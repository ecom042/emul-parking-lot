/*
 * Tests of services/screen with FFF fakes in place of hw/st7789. text.c and
 * lane_hw.c are the real ones, so the rendered pixels are checked against the
 * font, not against a copy of it.
 */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "hw/lane_hw.h"
#include "hw/st7789.h"
#include "services/screen.h"
#include "services/text.h"

/* DEFINE_FFF_GLOBALS lives in test_exemplo.c: one per test binary. */
FAKE_VALUE_FUNC(int, st7789_init, const struct st7789 *);
FAKE_VALUE_FUNC(int, st7789_set_window, const struct st7789 *, uint16_t, uint16_t, uint16_t,
		uint16_t);
FAKE_VALUE_FUNC(int, st7789_write_pixels, const struct st7789 *, const uint8_t *, size_t);
FAKE_VALUE_FUNC(int, st7789_fill, const struct st7789 *, uint16_t, uint16_t, uint16_t, uint16_t,
		uint16_t);

#define ROW_MAX (ST7789_WIDTH * 2)

/* The row buffer is reused by screen.c: copy the bytes of the chosen write. */
static unsigned int capture_at;
static unsigned int write_n;
static uint8_t captured[ROW_MAX];
static size_t captured_len;

static int fake_write(const struct st7789 *lcd, const uint8_t *data, size_t len)
{
	ARG_UNUSED(lcd);
	if (write_n == capture_at) {
		zassert_true(len <= sizeof(captured));
		memcpy(captured, data, len);
		captured_len = len;
	}
	write_n++;
	return 0;
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(st7789_init);
	RESET_FAKE(st7789_set_window);
	RESET_FAKE(st7789_write_pixels);
	RESET_FAKE(st7789_fill);
	FFF_RESET_HISTORY();
	st7789_write_pixels_fake.custom_fake = fake_write;
	capture_at = UINT32_MAX;
	write_n = 0;
	captured_len = 0;
}

ZTEST_SUITE(screen, NULL, NULL, reset, NULL, NULL);

static void expect_window(unsigned int call, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	zassert_equal(st7789_set_window_fake.arg1_history[call], x0);
	zassert_equal(st7789_set_window_fake.arg2_history[call], y0);
	zassert_equal(st7789_set_window_fake.arg3_history[call], x1);
	zassert_equal(st7789_set_window_fake.arg4_history[call], y1);
}

static void expect_fill(unsigned int call, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	zassert_equal(st7789_fill_fake.arg1_history[call], x0);
	zassert_equal(st7789_fill_fake.arg2_history[call], y0);
	zassert_equal(st7789_fill_fake.arg3_history[call], x1);
	zassert_equal(st7789_fill_fake.arg4_history[call], y1);
	zassert_equal(st7789_fill_fake.arg5_history[call], 0x0000);
}

static void expect_no_spi(void)
{
	zassert_equal(st7789_fill_fake.call_count, 0);
	zassert_equal(st7789_set_window_fake.call_count, 0);
	zassert_equal(st7789_write_pixels_fake.call_count, 0);
}

ZTEST(screen, test_init_uses_payment_display_and_clears)
{
	zassert_ok(screen_init());
	zassert_equal(st7789_init_fake.call_count, 1);
	zassert_equal_ptr(st7789_init_fake.arg0_val, payment_display_get());
	zassert_equal(st7789_fill_fake.call_count, 1);
	zassert_equal_ptr(st7789_fill_fake.arg0_val, payment_display_get());
	expect_fill(0, 0, 40, 239, 167);
}

ZTEST(screen, test_init_reports_init_error)
{
	st7789_init_fake.return_val = -EIO;
	zassert_equal(screen_init(), -EIO);
	zassert_equal(st7789_fill_fake.call_count, 0);
}

ZTEST(screen, test_init_reports_fill_error)
{
	st7789_fill_fake.return_val = -EBUSY;
	zassert_equal(screen_init(), -EBUSY);
}

ZTEST(screen, test_plate_clears_then_draws_centered)
{
	zassert_ok(screen_show_plate("ABC1D23"));
	zassert_equal(st7789_fill_fake.call_count, 1);
	zassert_equal(st7789_set_window_fake.call_count, 1);
	zassert_equal(st7789_write_pixels_fake.call_count, 24);
	expect_fill(0, 0, 40, 239, 167);
	expect_window(0, 57, 40, 182, 63);
	/* fill, set_window, then the 24 pixel rows, in this order */
	zassert_equal_ptr(fff.call_history[0], (void *)st7789_fill);
	zassert_equal_ptr(fff.call_history[1], (void *)st7789_set_window);
	for (unsigned int i = 2; i < 26; i++) {
		zassert_equal_ptr(fff.call_history[i], (void *)st7789_write_pixels);
	}
	zassert_equal(st7789_write_pixels_fake.arg2_val, 252);
}

ZTEST(screen, test_plate_row_pixels_match_font)
{
	uint8_t expected[ROW_MAX];

	capture_at = 3;
	zassert_ok(screen_show_plate("ABC1D23"));
	zassert_equal(captured_len, 252);
	zassert_ok(text_render_row("ABC1D23", 3, 3, 0xFFFF, 0x0000, expected, sizeof(expected)));
	zassert_mem_equal(captured, expected, 252);
}

ZTEST(screen, test_plate_rejects_bad_char_without_spi)
{
	zassert_equal(screen_show_plate("abc1d23"), -EINVAL);
	expect_no_spi();
}

ZTEST(screen, test_plate_rejects_too_wide_without_spi)
{
	/* 14 characters * 18 px = 252 > 240 */
	zassert_equal(screen_show_plate("ABCDEFGHIJKLMN"), -EINVAL);
	expect_no_spi();
}

ZTEST(screen, test_plate_propagates_fill_error)
{
	st7789_fill_fake.return_val = -EIO;
	zassert_equal(screen_show_plate("ABC1D23"), -EIO);
	zassert_equal(st7789_set_window_fake.call_count, 0);
}

ZTEST(screen, test_plate_propagates_set_window_error)
{
	st7789_set_window_fake.return_val = -EINVAL;
	zassert_equal(screen_show_plate("ABC1D23"), -EINVAL);
	zassert_equal(st7789_write_pixels_fake.call_count, 0);
}

ZTEST(screen, test_plate_propagates_write_error_and_stops)
{
	st7789_write_pixels_fake.custom_fake = NULL;
	st7789_write_pixels_fake.return_val = -EIO;
	zassert_equal(screen_show_plate("ABC1D23"), -EIO);
	zassert_equal(st7789_write_pixels_fake.call_count, 1);
}

ZTEST(screen, test_amount_draws_three_lines)
{
	zassert_ok(screen_show_amount(400));
	zassert_equal(st7789_fill_fake.call_count, 1);
	expect_fill(0, 0, 80, 239, 103);
	zassert_equal(st7789_set_window_fake.call_count, 3);
	expect_window(0, 48, 80, 191, 103);
	expect_window(1, 18, 128, 221, 143);
	expect_window(2, 78, 152, 161, 167);
	zassert_equal(st7789_write_pixels_fake.call_count, 24 + 16 + 16);
}

ZTEST(screen, test_amount_max_contract_fits)
{
	zassert_ok(screen_show_amount(1000000));
	expect_window(0, 12, 80, 227, 103);
	zassert_equal(st7789_write_pixels_fake.call_count, 56);
}

ZTEST(screen, test_amount_small_value_format)
{
	uint8_t expected[ROW_MAX];

	capture_at = 5;
	zassert_ok(screen_show_amount(5));
	expect_window(0, 48, 80, 191, 103);
	zassert_equal(captured_len, 288);
	zassert_ok(text_render_row("BRL 0.05", 3, 5, 0xFFFF, 0x0000, expected, sizeof(expected)));
	zassert_mem_equal(captured, expected, 288);
}

ZTEST(screen, test_amount_too_wide_rejected_without_spi)
{
	zassert_equal(screen_show_amount(UINT32_MAX), -EINVAL);
	expect_no_spi();
}

ZTEST(screen, test_amount_propagates_fill_error)
{
	st7789_fill_fake.return_val = -EIO;
	zassert_equal(screen_show_amount(400), -EIO);
	zassert_equal(st7789_set_window_fake.call_count, 0);
}

ZTEST(screen, test_amount_propagates_set_window_error_of_each_line)
{
	for (int fail = 0; fail < 3; fail++) {
		int seq[3] = {0, 0, 0};

		RESET_FAKE(st7789_set_window);
		RESET_FAKE(st7789_write_pixels);
		st7789_write_pixels_fake.custom_fake = fake_write;
		seq[fail] = -EIO;
		SET_RETURN_SEQ(st7789_set_window, seq, 3);
		zassert_equal(screen_show_amount(400), -EIO);
		zassert_equal(st7789_set_window_fake.call_count, fail + 1);
	}
}

ZTEST(screen, test_amount_propagates_write_error_and_stops)
{
	st7789_write_pixels_fake.custom_fake = NULL;
	st7789_write_pixels_fake.return_val = -EIO;
	zassert_equal(screen_show_amount(400), -EIO);
	zassert_equal(st7789_write_pixels_fake.call_count, 1);
	zassert_equal(st7789_set_window_fake.call_count, 1);
}
