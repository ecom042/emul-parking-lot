/* Mensagens da câmera (AT) e do terminal (P1). */
#include <string.h>

#include <zephyr/ztest.h>

#include "protocol.h"

#define UUID_A "3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1"

ZTEST_SUITE(protocol, NULL, NULL, NULL, NULL, NULL);

ZTEST(protocol, test_placas)
{
	zassert_true(plate_is_valid("ABC1D23", 7));
	zassert_true(plate_is_valid("ABC1234", 7));
	zassert_false(plate_is_valid("ABC1D2", 6));
	zassert_false(plate_is_valid("aBC1D23", 7));
	zassert_false(plate_is_valid("A1C1D23", 7));
	zassert_false(plate_is_valid("AB11D23", 7));
	zassert_false(plate_is_valid("ABCAD23", 7));
	zassert_false(plate_is_valid("ABC1-23", 7));
	zassert_false(plate_is_valid("ABC1DX3", 7));
	zassert_false(plate_is_valid("ABC1D2X", 7));
}

ZTEST(protocol, test_uuid)
{
	zassert_true(uuid_is_valid(UUID_A, 36));
	zassert_false(uuid_is_valid(UUID_A, 35));
	zassert_false(uuid_is_valid("3F2A9C10-7b4e-4d21-9a6f-0c5e8b7d12c1", 36));
	zassert_false(uuid_is_valid("3f2a9c10x7b4e-4d21-9a6f-0c5e8b7d12c1", 36));
	zassert_false(uuid_is_valid("3f2a9c1-07b4e-4d21-9a6f-0c5e8b7d12c1", 36));
}

ZTEST(protocol, test_decimal_canonico)
{
	uint32_t v = 0;

	zassert_true(parse_canonical_u32("0", 1, 0, 100, &v));
	zassert_equal(v, 0);
	zassert_true(parse_canonical_u32("100", 3, 0, 100, &v));
	zassert_false(parse_canonical_u32("101", 3, 0, 100, &v));
	zassert_false(parse_canonical_u32("0", 1, 1, 100, &v));
	zassert_false(parse_canonical_u32("07", 2, 0, 100, &v), "zero à esquerda");
	zassert_false(parse_canonical_u32("", 0, 0, 100, &v));
	zassert_false(parse_canonical_u32("1234567890", 10, 0, CAPTURE_ID_MAX, &v));
	zassert_false(parse_canonical_u32("1a", 2, 0, 100, &v));
	zassert_false(parse_canonical_u32("-1", 2, 0, 100, &v));
}

ZTEST(protocol, test_formata_captura)
{
	char buf[32];

	zassert_equal(camera_format_capture(buf, sizeof(buf), 17), 15);
	zassert_str_equal(buf, "AT+CAPTURE=17\r\n");
	zassert_equal(camera_format_capture(buf, 5, 17), -1, "não cabe");
}

ZTEST(protocol, test_placa_valida)
{
	struct plate_result r;

	zassert_ok(camera_parse_plate("+PLATE:17,\"ABC1D23\",93", &r));
	zassert_equal(r.capture_id, 17);
	zassert_str_equal(r.plate, "ABC1D23");
	zassert_equal(r.confidence, 93);
	zassert_ok(camera_parse_plate("+PLATE:1,\"ABC1234\",0", &r));
	zassert_equal(r.confidence, 0);
}

ZTEST(protocol, test_placa_malformada)
{
	struct plate_result r = {.capture_id = 5};
	const char *const bad[] = {
		"OK",
		"ERROR",
		"+PLATE:17",
		"+PLATE:17,ABC1D23,93",
		"+PLATE:17,\"ABC1D2\",93",
		"+PLATE:17,\"ABC1D23\"93",
		"+PLATE:017,\"ABC1D23\",93",
		"+PLATE:0,\"ABC1D23\",93",
		"+PLATE:17,\"abc1d23\",93",
		"+PLATE:17,\"ABC1D23\",101",
		"+PLATE:17,\"ABC1D23\",",
		"+PLATE:17,\"ABC1D23\",9 3",
		"+PLATE:17,\"AB",
	};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_equal(camera_parse_plate(bad[i], &r), -1, "%s", bad[i]);
	}
	zassert_equal(r.capture_id, 5, "saída intocada");
}

ZTEST(protocol, test_cobranca_e_cancelamento)
{
	char buf[128];

	zassert_true(payment_format_charge(buf, sizeof(buf), UUID_A, "ABC1D23", 400) > 0);
	zassert_str_equal(buf, "P1 CHARGE " UUID_A " ABC1D23 400 BRL\n");
	zassert_true(payment_format_cancel(buf, sizeof(buf), UUID_A) > 0);
	zassert_str_equal(buf, "P1 CANCEL " UUID_A "\n");
	zassert_equal(payment_format_charge(buf, 10, UUID_A, "ABC1D23", 400), -1);
}

ZTEST(protocol, test_uuid_gerado_e_valido_e_unico)
{
	char a[UUID_LEN + 1];
	char b[UUID_LEN + 1];

	uuid_make(a, 1, 5000);
	uuid_make(b, 2, 5000);
	zassert_true(uuid_is_valid(a, strlen(a)));
	zassert_true(uuid_is_valid(b, strlen(b)));
	zassert_true(strcmp(a, b) != 0);
}

ZTEST(protocol, test_resultado_valido)
{
	struct payment_result r;

	zassert_ok(payment_parse_result("P1 RESULT " UUID_A " PAID 400 BRL", &r));
	zassert_str_equal(r.uuid, UUID_A);
	zassert_equal(r.status, PAYMENT_PAID);
	zassert_equal(r.amount, 400);
	zassert_ok(payment_parse_result("P1 RESULT " UUID_A " CANCELLED 400 BRL", &r));
	zassert_equal(r.status, PAYMENT_CANCELLED);
}

ZTEST(protocol, test_resultado_malformado)
{
	struct payment_result r;
	const char *const bad[] = {
		"P1 RESULT " UUID_A " PAID broken BRL", /* falha MALFORMED do modelo */
		"P1 RESULT " UUID_A " PAID 400 USD",
		"P1 RESULT " UUID_A " PAID 0400 BRL",
		"P1 RESULT " UUID_A " PAID 0 BRL",
		"P1 RESULT " UUID_A " PAID 1000001 BRL",
		"P1 RESULT " UUID_A " REFUSED 400 BRL",
		"P1 RESULT " UUID_A "  PAID 400 BRL",
		"P1 RESULT " UUID_A " PAID 400 BRL extra",
		"P1 RESULT " UUID_A " PAID 400",
		"P2 RESULT " UUID_A " PAID 400 BRL",
		"P1 RESUME " UUID_A " PAID 400 BRL",
		"P1 RESULT 3F2A9C10-7B4E-4D21-9A6F-0C5E8B7D12C1 PAID 400 BRL",
		"P1 ERROR TIMEOUT",
	};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_equal(payment_parse_result(bad[i], &r), -1, "%s", bad[i]);
	}
}
