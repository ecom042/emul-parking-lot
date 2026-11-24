#include "services/screen.h"

#include <errno.h>

#include "hw/lane_hw.h"
#include "hw/st7789.h"
#include "services/text.h"

#define FG 0xFFFF /**< text color (white) */
#define BG 0x0000 /**< background color (black) */

/* The charge area: y 40..167, full width. */
#define AREA_Y0 40
#define AREA_Y1 167

#define PLATE_SCALE  3
#define PLATE_Y      40
#define AMOUNT_SCALE 3
#define AMOUNT_Y     80
#define LABEL_SCALE  2
#define LABEL_Y      128
#define NOTPIX_SCALE 2
#define NOTPIX_Y     152

#define LABEL_TEXT  "SIMULATED PAYMENT"
#define NOTPIX_TEXT "NOT PIX"

/* Width of a string literal in pixels (known at build time, always valid). */
#define LITERAL_W(s, scale) ((int)(sizeof(s) - 1) * TEXT_CELL_W * (scale))

/* One pixel row of the widest text (the full display width, RGB565). */
static uint8_t row_buf[ST7789_WIDTH * 2];

/* Width of @p s if it can be drawn on the display, else -EINVAL. */
static int measure(const char *s, unsigned int scale)
{
	int w = text_width(s, scale);

	if (w < 0) {
		return w;
	}
	return w > ST7789_WIDTH ? -EINVAL : w;
}

/* Draw @p s (of width @p w, already measured) centered at row @p y. */
static int draw_centered(const struct st7789 *lcd, const char *s, unsigned int scale,
			 uint16_t y, int w)
{
	uint16_t x = (ST7789_WIDTH - w) / 2;
	int rc = st7789_set_window(lcd, x, y, x + w - 1, y + TEXT_CELL_H * scale - 1);

	for (unsigned int r = 0; rc == 0 && r < TEXT_CELL_H * scale; r++) {
		/*
		 * Cannot fail: @p s was validated by measure(), r is in range
		 * and row_buf holds a full display row (w <= ST7789_WIDTH).
		 */
		(void)text_render_row(s, scale, r, FG, BG, row_buf, sizeof(row_buf));
		rc = st7789_write_pixels(lcd, row_buf, w * 2);
	}
	return rc;
}

int screen_init(void)
{
	const struct st7789 *lcd = payment_display_get();
	int rc = st7789_init(lcd);

	if (rc != 0) {
		return rc;
	}
	return st7789_fill(lcd, 0, AREA_Y0, ST7789_WIDTH - 1, AREA_Y1, BG);
}

int screen_show_plate(const char *plate)
{
	const struct st7789 *lcd = payment_display_get();
	int w = measure(plate, PLATE_SCALE);
	int rc;

	if (w < 0) {
		return -EINVAL; /* nothing is sent: the old screen stays intact */
	}
	rc = st7789_fill(lcd, 0, AREA_Y0, ST7789_WIDTH - 1, AREA_Y1, BG);
	if (rc != 0) {
		return rc;
	}
	return draw_centered(lcd, plate, PLATE_SCALE, PLATE_Y, w);
}

int screen_show_amount(uint32_t cents)
{
	const struct st7789 *lcd = payment_display_get();
	/* 16 bytes hold the largest uint32_t amount ("BRL 42949672.95" + NUL). */
	char amount[16];
	int rc;
	int w;

	/* Cannot fail: 16 bytes hold the largest uint32_t amount. */
	(void)text_format_brl(cents, amount, sizeof(amount));
	w = measure(amount, AMOUNT_SCALE);
	if (w < 0) {
		return w; /* too wide for the display: nothing is sent */
	}
	rc = st7789_fill(lcd, 0, AMOUNT_Y, ST7789_WIDTH - 1,
			 AMOUNT_Y + TEXT_CELL_H * AMOUNT_SCALE - 1, BG);
	if (rc == 0) {
		rc = draw_centered(lcd, amount, AMOUNT_SCALE, AMOUNT_Y, w);
	}
	if (rc == 0) {
		rc = draw_centered(lcd, LABEL_TEXT, LABEL_SCALE, LABEL_Y,
				   LITERAL_W(LABEL_TEXT, LABEL_SCALE));
	}
	if (rc == 0) {
		rc = draw_centered(lcd, NOTPIX_TEXT, NOTPIX_SCALE, NOTPIX_Y,
				   LITERAL_W(NOTPIX_TEXT, NOTPIX_SCALE));
	}
	return rc;
}
