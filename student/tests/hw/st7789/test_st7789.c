/*
 * Tests of hw/st7789. The SPI bytes cannot be read back from software, so
 * these tests cover the argument checks and that the real board path works;
 * the pixels themselves are judged by the display model in the scenarios.
 */
#include <errno.h>

#include <zephyr/ztest.h>

#include "hw/st7789.h"

static const struct st7789 lcd = ST7789_DT_GET(DT_ALIAS(payment_display));

ZTEST_SUITE(hw_st7789, NULL, NULL, NULL, NULL, NULL);

ZTEST(hw_st7789, test_set_window_rejects_invalid_windows_before_touching_the_bus)
{
	zassert_equal(st7789_set_window(&lcd, 10, 0, 9, 5), -EINVAL, "x0 > x1");
	zassert_equal(st7789_set_window(&lcd, 0, 10, 5, 9), -EINVAL, "y0 > y1");
	zassert_equal(st7789_set_window(&lcd, 0, 0, ST7789_WIDTH, 5), -EINVAL, "x1 too big");
	zassert_equal(st7789_set_window(&lcd, 0, 0, 5, ST7789_HEIGHT), -EINVAL, "y1 too big");
	zassert_equal(st7789_fill(&lcd, 5, 0, 4, 0, 0), -EINVAL);
}

ZTEST(hw_st7789, test_init_window_and_fill_succeed_on_the_board_path)
{
	zassert_ok(st7789_init(&lcd));
	zassert_ok(st7789_set_window(&lcd, 0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1));
	zassert_ok(st7789_fill(&lcd, 0, 0, 9, 9, 0xF800));
	zassert_ok(st7789_fill(&lcd, 0, 0, 0, 0, 0x001F));
}
