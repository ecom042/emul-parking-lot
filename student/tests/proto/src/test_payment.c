/*
 * Testes da tarifa (tariff.c) e do protocolo P1 (payment.c), REQ-05/11/12.
 */
#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "config.h"
#include "payment.h"
#include "tariff.h"

#define U1 "3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1"

ZTEST_SUITE(tariff, NULL, NULL, NULL, NULL, NULL);

ZTEST(tariff, test_examples_from_the_spec)
{
	uint32_t c;

	zassert_ok(tariff_compute(0, 60000, &c));
	zassert_equal(c, 200, "mínimo de uma unidade");
	zassert_ok(tariff_compute(60000, 60000, &c));
	zassert_equal(c, 200);
	zassert_ok(tariff_compute(60001, 60000, &c));
	zassert_equal(c, 300);
	zassert_ok(tariff_compute(2500, 1000, &c));
	zassert_equal(c, 400, "2,5 s com unidade de 1 s = 3 unidades");
	zassert_ok(tariff_compute(-5, 1000, &c));
	zassert_equal(c, 200);
}

ZTEST(tariff, test_unit_limit)
{
	uint32_t c;

	zassert_ok(tariff_compute((int64_t)TARIFF_MAX_UNITS * 1000, 1000, &c));
	zassert_equal(c, 200 + (TARIFF_MAX_UNITS - 1) * 100);
	zassert_equal(tariff_compute((int64_t)TARIFF_MAX_UNITS * 1000 + 1, 1000, &c), -ERANGE);
	zassert_equal(tariff_compute(1000, 0, &c), -EINVAL);
}

ZTEST(tariff, test_format_brl)
{
	char buf[24];

	zassert_equal(tariff_format_brl(buf, sizeof(buf), 400), 8);
	zassert_str_equal(buf, "BRL 4.00");
	tariff_format_brl(buf, sizeof(buf), 1005);
	zassert_str_equal(buf, "BRL 10.05");
	tariff_format_brl(buf, sizeof(buf), 1000000);
	zassert_str_equal(buf, "BRL 10000.00");
	zassert_equal(tariff_format_brl(buf, 5, 400), -EINVAL);
}

ZTEST_SUITE(payment, NULL, NULL, NULL, NULL, NULL);

ZTEST(payment, test_format_charge_and_cancel)
{
	char buf[128];

	zassert_true(payment_format_charge(buf, sizeof(buf), U1, "ABC1D23", 400) > 0);
	zassert_str_equal(buf, "P1 CHARGE " U1 " ABC1D23 400 BRL\n");
	zassert_true(payment_format_cancel(buf, sizeof(buf), U1) > 0);
	zassert_str_equal(buf, "P1 CANCEL " U1 "\n");

	zassert_equal(payment_format_charge(buf, sizeof(buf), "x", "ABC1D23", 400), -EINVAL);
	zassert_equal(payment_format_charge(buf, sizeof(buf), U1, "abc", 400), -EINVAL);
	zassert_equal(payment_format_charge(buf, sizeof(buf), U1, "ABC1D23", 0), -EINVAL);
	zassert_equal(payment_format_charge(buf, sizeof(buf), U1, "ABC1D23", 1000001), -EINVAL);
	zassert_equal(payment_format_charge(buf, 20, U1, "ABC1D23", 400), -EINVAL);
	zassert_equal(payment_format_cancel(buf, sizeof(buf), "x"), -EINVAL);
	zassert_equal(payment_format_cancel(buf, 20, U1), -EINVAL);
}

ZTEST(payment, test_parse_paid_and_cancelled)
{
	struct payment_msg m;

	payment_parse("P1 RESULT " U1 " PAID 400 BRL", &m);
	zassert_equal(m.kind, PAYMENT_PAID);
	zassert_str_equal(m.uuid, U1);
	zassert_equal(m.amount, 400);

	payment_parse("P1 RESULT " U1 " CANCELLED 1000000 BRL", &m);
	zassert_equal(m.kind, PAYMENT_CANCELLED);
	zassert_equal(m.amount, 1000000);
}

ZTEST(payment, test_parse_errors)
{
	struct payment_msg m;

	payment_parse("P1 ERROR TIMEOUT", &m);
	zassert_equal(m.kind, PAYMENT_ERROR);
	payment_parse("P1 ERROR CONFLICT", &m);
	zassert_equal(m.kind, PAYMENT_ERROR);
	payment_parse("P1 ERROR OTHER", &m);
	zassert_equal(m.kind, PAYMENT_INVALID);
}

ZTEST(payment, test_parse_malformed)
{
	static const char *const bad[] = {
		"P1 RESULT " U1 " PAID broken BRL",      /* falha MALFORMED do modelo */
		"P1 RESULT " U1 " PAID 0400 BRL",
		"P1 RESULT " U1 " PAID 0 BRL",
		"P1 RESULT " U1 " PAID 1000001 BRL",
		"P1 RESULT " U1 " PAID 400 brl",
		"P1 RESULT " U1 " PAID 400 BRL ",
		"P1 RESULT " U1 "  PAID 400 BRL",
		"P1 RESULT " U1 " PAID 400",
		"P1 RESULT " U1 " paid 400 BRL",
		"P1 RESULT " U1 " REFUND 400 BRL",
		"P1 RESULT 3F2A9C10-7B4E-4D21-9A6F-0C5E8B7D12C1 PAID 400 BRL",
		"P1 RESULT 3f2a9c107b4e4d219a6f0c5e8b7d12c1 PAID 400 BRL",
		"P1 RESULT 3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c PAID 400 BRL",
		"P1 RESULT " U1,
		"P1 RESULT ",
		"P1  RESULT " U1 " PAID 400 BRL",
		"P2 RESULT " U1 " PAID 400 BRL",
		"",
	};
	struct payment_msg m;

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		payment_parse(bad[i], &m);
		zassert_equal(m.kind, PAYMENT_INVALID, "aceitou: %s", bad[i]);
	}
}

ZTEST(payment, test_uuid_format_and_uniqueness)
{
	char a[UUID_BUF];
	char b[UUID_BUF];

	payment_new_uuid(a, 1234);
	payment_new_uuid(b, 1234); /* mesma entropia: o contador muda o UUID */
	zassert_equal(strlen(a), UUID_LEN);
	zassert_true(payment_uuid_valid(a), "%s", a);
	zassert_true(payment_uuid_valid(b), "%s", b);
	zassert_true(strcmp(a, b) != 0);
	zassert_equal(a[14], '4', "versão 4");
}

ZTEST(payment, test_uuid_validation)
{
	zassert_true(payment_uuid_valid(U1));
	zassert_false(payment_uuid_valid("3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1x"));
	zassert_false(payment_uuid_valid("3f2a9c10_7b4e-4d21-9a6f-0c5e8b7d12c1"));
	zassert_false(payment_uuid_valid("3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12cg"));
	zassert_false(payment_uuid_valid(""));
}
