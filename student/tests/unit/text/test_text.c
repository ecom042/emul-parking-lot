/* Tests of services/text: BRL format and the bitmap font. */
#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "services/text.h"

#define FG 0xF800
#define BG 0x0000

ZTEST_SUITE(text, NULL, NULL, NULL, NULL, NULL);

ZTEST(text, test_format_brl)
{
	char out[24];
	const struct { uint32_t cents; const char *text; } cases[] = {
		{0, "BRL 0.00"}, {5, "BRL 0.05"}, {200, "BRL 2.00"}, {1230, "BRL 12.30"},
		{100000, "BRL 1000.00"}, {1000000, "BRL 10000.00"},
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		int n = text_format_brl(cases[i].cents, out, sizeof(out));

		zassert_equal(n, (int)strlen(cases[i].text));
		zassert_str_equal(out, cases[i].text);
	}
	zassert_equal(text_format_brl(200, out, 4), -ENOSPC);
}

ZTEST(text, test_width)
{
	zassert_equal(text_width("A", 1), 6);
	zassert_equal(text_width("AB", 2), 24);
	zassert_equal(text_width("SIMULATED PAYMENT", 2), 204);
	zassert_equal(text_width("", 1), 0);
	zassert_equal(text_width("a", 1), -EINVAL);
	zassert_equal(text_width("A-B", 1), -EINVAL);
	zassert_equal(text_width("A", 0), -EINVAL);
}

ZTEST(text, test_every_supported_character_has_a_glyph)
{
	zassert_true(text_width("ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789.", 1) > 0);
}

ZTEST(text, test_render_row_of_I_scale_1)
{
	uint8_t out[12];

	/* "I" columns: 00 41 7F 41 00. Row 0: columns 1, 2 and 3 are lit (bit 0 of each). */
	const uint8_t row0[12] = {0, 0, 0xF8, 0, 0xF8, 0, 0xF8, 0, 0, 0, 0, 0};
	const uint8_t row3[12] = {0, 0, 0, 0, 0xF8, 0, 0, 0, 0, 0, 0, 0};

	zassert_ok(text_render_row("I", 1, 0, FG, BG, out, sizeof(out)));
	zassert_mem_equal(out, row0, sizeof(row0));

	/* Row 3: only the stem (0x7F) has bit 3 set. */
	zassert_ok(text_render_row("I", 1, 3, FG, BG, out, sizeof(out)));
	zassert_mem_equal(out, row3, sizeof(row3));
}

ZTEST(text, test_render_row_blank_row_and_blank_column)
{
	uint8_t out[24];

	zassert_ok(text_render_row("AA", 1, 7, FG, BG, out, sizeof(out)));
	for (size_t i = 0; i < sizeof(out); i++) {
		zassert_equal(out[i], 0, "row 7 is blank");
	}
	zassert_ok(text_render_row("AA", 1, 3, FG, BG, out, sizeof(out)));
	zassert_equal(out[10], 0);
	zassert_equal(out[11], 0, "6th column of the cell is blank");
}

ZTEST(text, test_render_row_scale_2_repeats_pixels_and_rows)
{
	uint8_t one[12], two[24];

	zassert_ok(text_render_row("I", 1, 0, FG, BG, one, sizeof(one)));
	zassert_ok(text_render_row("I", 2, 0, FG, BG, two, sizeof(two)));
	for (size_t x = 0; x < 6; x++) {
		zassert_mem_equal(&two[4 * x], &one[2 * x], 2);
		zassert_mem_equal(&two[4 * x + 2], &one[2 * x], 2);
	}
	uint8_t second[24];

	zassert_ok(text_render_row("I", 2, 1, FG, BG, second, sizeof(second)));
	zassert_mem_equal(second, two, sizeof(two));
}

ZTEST(text, test_render_row_errors)
{
	uint8_t out[12];

	zassert_equal(text_render_row("I", 1, 8, FG, BG, out, sizeof(out)), -EINVAL);
	zassert_equal(text_render_row("I", 2, 16, FG, BG, out, sizeof(out)), -EINVAL);
	zassert_equal(text_render_row("a", 1, 0, FG, BG, out, sizeof(out)), -EINVAL);
	zassert_equal(text_render_row("I", 1, 0, FG, BG, out, 11), -ENOSPC);
}
