#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#include "camera.h"
#include "payment_display.h"
#include "payment_display_hw.h"

enum {
	SWRESET = 0x01,
	SLPOUT = 0x11,
	DISPON = 0x29,
	CASET = 0x2a,
	RASET = 0x2b,
	RAMWR = 0x2c,
	MADCTL = 0x36,
	COLMOD = 0x3a,
};

enum {
	GLYPH_W = 5,
	GLYPH_H = 7,
	CELL_W = GLYPH_W + 1,
	SCALE = 2,
	TEXT_X = 18,
	PLATE_Y = 36,
	AMOUNT_Y = 108,
	SIMULATED_Y = 170,
	NOT_PIX_Y = 196,
	MAX_AMOUNT = 1000000,
	TEXT_RGB565 = 0x07ff,
};

static const char simulated[] = "SIMULATED PAYMENT";
static const char not_pix[] = "NOT PIX";

/* One row per glyph. The low five bits of each row run from right to left. */
static const uint8_t glyphs['Z' + 1][GLYPH_H] = {
	['.'] = { 0, 0, 0, 0, 0, 12, 12 },
	['0'] = { 14, 17, 19, 21, 25, 17, 14 },
	['1'] = { 4, 12, 4, 4, 4, 4, 14 },
	['2'] = { 14, 17, 1, 2, 4, 8, 31 },
	['3'] = { 30, 1, 1, 14, 1, 1, 30 },
	['4'] = { 2, 6, 10, 18, 31, 2, 2 },
	['5'] = { 31, 16, 16, 30, 1, 1, 30 },
	['6'] = { 6, 8, 16, 30, 17, 17, 14 },
	['7'] = { 31, 1, 2, 4, 8, 8, 8 },
	['8'] = { 14, 17, 17, 14, 17, 17, 14 },
	['9'] = { 14, 17, 17, 15, 1, 2, 12 },
	['A'] = { 14, 17, 17, 31, 17, 17, 17 },
	['B'] = { 30, 17, 17, 30, 17, 17, 30 },
	['C'] = { 14, 17, 16, 16, 16, 17, 14 },
	['D'] = { 30, 17, 17, 17, 17, 17, 30 },
	['E'] = { 31, 16, 16, 30, 16, 16, 31 },
	['F'] = { 31, 16, 16, 30, 16, 16, 16 },
	['G'] = { 14, 17, 16, 23, 17, 17, 15 },
	['H'] = { 17, 17, 17, 31, 17, 17, 17 },
	['I'] = { 14, 4, 4, 4, 4, 4, 14 },
	['J'] = { 7, 2, 2, 2, 18, 18, 12 },
	['K'] = { 17, 18, 20, 24, 20, 18, 17 },
	['L'] = { 16, 16, 16, 16, 16, 16, 31 },
	['M'] = { 17, 27, 21, 21, 17, 17, 17 },
	['N'] = { 17, 25, 21, 19, 17, 17, 17 },
	['O'] = { 14, 17, 17, 17, 17, 17, 14 },
	['P'] = { 30, 17, 17, 30, 16, 16, 16 },
	['Q'] = { 14, 17, 17, 17, 21, 18, 13 },
	['R'] = { 30, 17, 17, 30, 20, 18, 17 },
	['S'] = { 15, 16, 16, 14, 1, 1, 30 },
	['T'] = { 31, 4, 4, 4, 4, 4, 4 },
	['U'] = { 17, 17, 17, 17, 17, 17, 14 },
	['V'] = { 17, 17, 17, 17, 17, 10, 4 },
	['W'] = { 17, 17, 17, 21, 21, 21, 10 },
	['X'] = { 17, 17, 10, 4, 10, 17, 17 },
	['Y'] = { 17, 17, 10, 4, 4, 4, 4 },
	['Z'] = { 31, 1, 2, 4, 8, 16, 31 },
};

/* A single line of RGB565; only the exit worker accesses it. */
static uint8_t scanline[(sizeof(simulated) - 1) * CELL_W * SCALE * sizeof(uint16_t)];

static int command(uint8_t cmd, uint8_t *params, size_t len)
{
	int rc = payment_display_hw_write(false, &cmd, sizeof(cmd));

	if (rc == 0 && len != 0) {
		rc = payment_display_hw_write(true, params, len);
	}
	return rc;
}

int payment_display_init(void)
{
	int rc = payment_display_hw_prepare();

	if (rc != 0) {
		return rc;
	}
	rc = payment_display_hw_reset(true);
	if (rc != 0) {
		return rc;
	}
	k_msleep(10);
	rc = payment_display_hw_reset(false);
	if (rc != 0) {
		return rc;
	}
	k_msleep(120);

	static const struct {
		uint8_t cmd;
		uint8_t data;
		uint8_t len;
		int wait_ms;
	} setup[] = {
		{ SWRESET, 0, 0, 150 },
		{ SLPOUT, 0, 0, 120 },
		{ COLMOD, 0x55, 1, 0 },
		{ MADCTL, 0x00, 1, 0 },
		{ DISPON, 0, 0, 0 },
	};

	for (size_t i = 0; i < ARRAY_SIZE(setup); i++) {
		uint8_t value = setup[i].data;

		rc = command(setup[i].cmd, &value, setup[i].len);
		if (rc != 0) {
			return rc;
		}
		if (setup[i].wait_ms != 0) {
			k_msleep(setup[i].wait_ms);
		}
	}
	return 0;
}

static int set_region(uint16_t y, uint16_t width)
{
	uint8_t bounds[4];

	sys_put_be16(TEXT_X, bounds);
	sys_put_be16(TEXT_X + width - 1, bounds + 2);
	int rc = command(CASET, bounds, sizeof(bounds));

	if (rc != 0) {
		return rc;
	}
	sys_put_be16(y, bounds);
	sys_put_be16(y + GLYPH_H * SCALE - 1, bounds + 2);
	rc = command(RASET, bounds, sizeof(bounds));
	if (rc != 0) {
		return rc;
	}
	return command(RAMWR, NULL, 0);
}

static int draw_text(uint16_t y, const char *text, size_t length, size_t cells)
{
	uint16_t width = cells * CELL_W * SCALE;
	int rc = set_region(y, width);

	if (rc != 0) {
		return rc;
	}

	for (size_t glyph_y = 0; glyph_y < GLYPH_H; glyph_y++) {
		size_t offset = 0;

		for (size_t c = 0; c < cells; c++) {
			unsigned char ch = c < length ? (unsigned char)text[c] : ' ';
			uint8_t row = glyphs[ch][glyph_y];

			for (size_t x = 0; x < CELL_W; x++) {
				/* Shift the five pixels left to leave one blank column. */
				bool lit = ((row << 1) & BIT(CELL_W - 1 - x)) != 0;
				uint16_t color = lit ? TEXT_RGB565 : 0;

				for (size_t sx = 0; sx < SCALE; sx++) {
					sys_put_be16(color, scanline + offset);
					offset += sizeof(color);
				}
			}
		}
		for (size_t sy = 0; sy < SCALE; sy++) {
			rc = payment_display_hw_write(true, scanline, offset);
			if (rc != 0) {
				return rc;
			}
		}
	}
	return 0;
}

int payment_display_show_plate(const char plate[PARKING_PLATE_SIZE])
{
	size_t length = PARKING_PLATE_SIZE - 1;

	if (plate == NULL || strnlen(plate, PARKING_PLATE_SIZE) != length ||
	    !camera_plate_valid(plate, length)) {
		return -EINVAL;
	}
	return draw_text(PLATE_Y, plate, length, length);
}

int payment_display_show_amount(uint32_t cents)
{
	if (cents == 0 || cents > MAX_AMOUNT) {
		return -EINVAL;
	}
	char amount[sizeof("BRL 10000.00")];
	int length = snprintk(amount, sizeof(amount), "BRL %u.%02u",
			      (unsigned int)(cents / 100), (unsigned int)(cents % 100));
	/* The price field covers the old amount even if the new text is shorter. */
	int rc = draw_text(AMOUNT_Y, amount, length, sizeof(amount) - 1);

	if (rc != 0) {
		return rc;
	}
	rc = draw_text(SIMULATED_Y, simulated, sizeof(simulated) - 1, sizeof(simulated) - 1);
	if (rc != 0) {
		return rc;
	}
	return draw_text(NOT_PIX_Y, not_pix, sizeof(not_pix) - 1, sizeof(not_pix) - 1);
}
