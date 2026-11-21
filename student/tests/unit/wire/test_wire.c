/* Tests of services/wire: canonical numbers, plates and UUIDs. */
#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "services/wire.h"

ZTEST_SUITE(wire, NULL, NULL, NULL, NULL, NULL);

static int parse(const char *s, uint32_t max, uint32_t *out)
{
	return wire_parse_uint(s, strlen(s), max, out);
}

ZTEST(wire, test_uint_accepts_canonical_numbers)
{
	uint32_t v = 99;

	zassert_ok(parse("0", 10, &v));
	zassert_equal(v, 0);
	zassert_ok(parse("1", 10, &v));
	zassert_equal(v, 1);
	zassert_ok(parse("999999999", 999999999u, &v));
	zassert_equal(v, 999999999u);
}

ZTEST(wire, test_uint_rejects_non_canonical_text)
{
	uint32_t v;
	const char *bad[] = {"", "01", "00", "+1", "-1", "1a", " 1", "1 ", "1.0"};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_equal(parse(bad[i], 1000, &v), -EINVAL, "%s", bad[i]);
	}
}

ZTEST(wire, test_uint_rejects_values_above_max_without_overflow)
{
	uint32_t v;

	zassert_equal(parse("11", 10, &v), -ERANGE);
	zassert_equal(parse("1000000000", 999999999u, &v), -ERANGE);
	zassert_equal(parse("4294967296", UINT32_MAX, &v), -ERANGE);
	zassert_equal(parse("99999999999999999999", UINT32_MAX, &v), -ERANGE);
}

ZTEST(wire, test_plate_accepts_old_and_mercosur_formats)
{
	zassert_true(wire_is_plate("ABC1D23", 7));
	zassert_true(wire_is_plate("ABC1234", 7));
}

ZTEST(wire, test_plate_rejects_every_other_shape)
{
	const char *bad[] = {"abc1d23", "ABC-1D23", "AB1CD23", "ABC1D2", "ABC1D234",
			     "ABC1D2A", "1BC1D23", "ABC1d23", "ABC1_23", "       "};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_false(wire_is_plate(bad[i], strlen(bad[i])), "%s", bad[i]);
	}
}

/* One plate wrong at each position: every condition of the check can fail. */
ZTEST(wire, test_plate_rejects_a_wrong_character_at_each_position)
{
	const char *wrong_at[PLATE_LEN] = {
		"1BC1D23", /* 0: letter */
		"A1C1D23", /* 1: letter */
		"AB1CD23", /* 2: letter */
		"ABCAD23", /* 3: digit */
		"ABC1_23", /* 4: letter or digit */
		"ABC1DA3", /* 5: digit */
		"ABC1D2A", /* 6: digit */
	};

	for (size_t i = 0; i < ARRAY_SIZE(wrong_at); i++) {
		zassert_false(wire_is_plate(wrong_at[i], PLATE_LEN), "position %u", i);
	}
}

ZTEST(wire, test_uuid_accepts_lowercase_8_4_4_4_12)
{
	const char *uuid = "123e4567-e89b-12d3-a456-426614174000";

	zassert_true(wire_is_uuid(uuid, strlen(uuid)));
}

ZTEST(wire, test_uuid_rejects_bad_shapes)
{
	const char *bad[] = {
		"123E4567-e89b-12d3-a456-426614174000",  /* uppercase */
		"123e4567e-89b-12d3-a456-426614174000",  /* dash misplaced */
		"123e4567-e89b-12d3-a456-42661417400",   /* 35 chars */
		"123e4567-e89b-12d3-a456-4266141740000", /* 37 chars */
		"123e4567-e89b-12d3-a456-42661417400g",  /* not hex */
		"123e4567_e89b-12d3-a456-426614174000",  /* wrong separator */
	};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_false(wire_is_uuid(bad[i], strlen(bad[i])), "%s", bad[i]);
	}
}
