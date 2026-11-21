/* Tests of services/p1_proto: CHARGE/CANCEL formats and reply syntax. */
#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "services/p1_proto.h"

#define UUID "123e4567-e89b-12d3-a456-426614174000"

ZTEST_SUITE(p1_proto, NULL, NULL, NULL, NULL, NULL);

static struct p1_reply parse(const char *line)
{
	struct p1_reply r;

	p1_proto_parse(line, &r);
	return r;
}

ZTEST(p1_proto, test_uuid_format_shape)
{
	char u[UUID_LEN + 1];

	p1_uuid_format(7, u);
	zassert_true(wire_is_uuid(u, strlen(u)));
	zassert_equal(strlen(u), UUID_LEN);
	zassert_equal(u[8], '-');
	zassert_equal(u[13], '-');
	zassert_equal(u[18], '-');
	zassert_equal(u[23], '-');
}

ZTEST(p1_proto, test_uuid_format_values)
{
	char a[UUID_LEN + 1];
	char b[UUID_LEN + 1];

	p1_uuid_format(0, a);
	zassert_str_equal(a, "00000000-0000-4000-8000-000000000000");
	p1_uuid_format(0xABCDEF, b);
	zassert_str_equal(b, "00000000-0000-4000-8000-000000abcdef");
	zassert_true(strcmp(a, b) != 0);
	p1_uuid_format(1, a);
	p1_uuid_format(2, b);
	zassert_true(strcmp(a, b) != 0);
}

ZTEST(p1_proto, test_uuid_format_composes_with_charge)
{
	char u[UUID_LEN + 1];
	char out[128];

	p1_uuid_format(5, u);
	zassert_true(p1_proto_format_charge(u, "ABC1D23", 300, out, sizeof(out)) > 0);
	zassert_str_equal(out, "P1 CHARGE 00000000-0000-4000-8000-000000000005 ABC1D23 300 BRL\n");
}

ZTEST(p1_proto, test_format_charge)
{
	char out[128];
	int n = p1_proto_format_charge(UUID, "ABC1D23", 300, out, sizeof(out));

	zassert_equal(n, (int)strlen("P1 CHARGE " UUID " ABC1D23 300 BRL\n"));
	zassert_str_equal(out, "P1 CHARGE " UUID " ABC1D23 300 BRL\n");
}

ZTEST(p1_proto, test_format_charge_rejects_bad_fields)
{
	char out[128];

	zassert_equal(p1_proto_format_charge(UUID, "ABC1D23", 0, out, sizeof(out)), -EINVAL);
	zassert_equal(p1_proto_format_charge(UUID, "ABC1D23", 1000001, out, sizeof(out)),
		      -EINVAL);
	zassert_equal(p1_proto_format_charge(UUID, "abc1d23", 300, out, sizeof(out)), -EINVAL);
	zassert_equal(p1_proto_format_charge("not-a-uuid", "ABC1D23", 300, out, sizeof(out)),
		      -EINVAL);
	zassert_equal(p1_proto_format_charge(UUID, "ABC1D23", 300, out, 10), -ENOSPC);
	zassert_true(p1_proto_format_charge(UUID, "ABC1D23", 1000000, out, sizeof(out)) > 0);
}

ZTEST(p1_proto, test_format_cancel)
{
	char out[64];

	zassert_equal(p1_proto_format_cancel(UUID, out, sizeof(out)), 47);
	zassert_str_equal(out, "P1 CANCEL " UUID "\n");
	zassert_equal(p1_proto_format_cancel("x", out, sizeof(out)), -EINVAL);
	zassert_equal(p1_proto_format_cancel(UUID, out, 8), -ENOSPC);
}

ZTEST(p1_proto, test_paid_and_cancelled)
{
	struct p1_reply r = parse("P1 RESULT " UUID " PAID 300 BRL");

	zassert_equal(r.kind, P1_REPLY_PAID);
	zassert_str_equal(r.uuid, UUID);
	zassert_equal(r.amount, 300);
	r = parse("P1 RESULT " UUID " CANCELLED 1000000 BRL");
	zassert_equal(r.kind, P1_REPLY_CANCELLED);
	zassert_equal(r.amount, 1000000);
}

ZTEST(p1_proto, test_error_codes)
{
	const char *lines[] = {"P1 ERROR FRAME", "P1 ERROR TIMEOUT", "P1 ERROR CAPACITY",
			       "P1 ERROR CONFLICT", "P1 ERROR UNKNOWN"};
	enum p1_error codes[] = {P1_ERR_FRAME, P1_ERR_TIMEOUT, P1_ERR_CAPACITY,
				 P1_ERR_CONFLICT, P1_ERR_UNKNOWN};

	for (size_t i = 0; i < ARRAY_SIZE(lines); i++) {
		struct p1_reply r = parse(lines[i]);

		zassert_equal(r.kind, P1_REPLY_ERROR, "%s", lines[i]);
		zassert_equal(r.error, codes[i]);
	}
}

ZTEST(p1_proto, test_malformed_replies_are_invalid)
{
	const char *bad[] = {
		"", "P1", "P1 ", "P2 ERROR FRAME", "P1 ERROR", "P1 ERROR XYZ",
		"P1 ERROR frame", "P1 ERROR FRAME ", "P1  ERROR FRAME", " P1 ERROR FRAME",
		"P1 ERROR FRAME EXTRA", "P1 HELLO",
		"P1 RESULT " UUID " paid 300 BRL",
		"P1 RESULT " UUID " REFUND 300 BRL",
		"P1 RESULT " UUID " PAID 300 brl",
		"P1 RESULT " UUID " PAID 300 USD",
		"P1 RESULT " UUID " PAID 3.00 BRL",
		"P1 RESULT " UUID " PAID 0300 BRL",
		"P1 RESULT " UUID " PAID 0 BRL",
		"P1 RESULT " UUID " PAID 1000001 BRL",
		"P1 RESULT 123E4567-e89b-12d3-a456-426614174000 PAID 300 BRL",
		"P1 RESULT " UUID "  PAID 300 BRL",
		"P1 RESULT " UUID " PAID 300",
		"P1 RESULT " UUID " PAID 300 BRL EXTRA",
	};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_equal(parse(bad[i]).kind, P1_REPLY_INVALID, "%s", bad[i]);
	}
}

ZTEST(p1_proto, test_line_longer_than_a_frame_is_invalid)
{
	char line[200];

	memset(line, 'A', sizeof(line) - 1);
	line[sizeof(line) - 1] = '\0';
	zassert_equal(parse(line).kind, P1_REPLY_INVALID);
}
