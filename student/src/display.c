/*
 * display.c — ST7789V display driver via SPI
 *
 * Uses the hardware abstraction in hw.h for SPI/GPIO operations.
 * Renders text using the 8x16 bitmap font.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "display.h"
#include "hw.h"
#include "font.h"
#include <string.h>
#include <stdio.h>
#include <zephyr/kernel.h>

/* ST7789V commands */
#define ST7789_SWRESET  0x01
#define ST7789_SLPOUT   0x11
#define ST7789_DISPOFF  0x28
#define ST7789_DISPON   0x29
#define ST7789_COLMOD   0x3A
#define ST7789_MADCTL   0x36
#define ST7789_CASET    0x2A
#define ST7789_RASET    0x2B
#define ST7789_RAMWR    0x2C

/* RGB565 color constants */
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_GREEN     0x07E0
#define COLOR_YELLOW    0xFFE0

int display_init(void)
{
	int rc;

	/* Hardware reset */
	rc = hw_display_reset();
	if (rc != 0) {
		return rc;
	}

	/* Software reset */
	rc = hw_display_write_cmd(ST7789_SWRESET);
	if (rc != 0) {
		return rc;
	}
	k_msleep(150);

	/* Sleep out */
	rc = hw_display_write_cmd(ST7789_SLPOUT);
	if (rc != 0) {
		return rc;
	}
	k_msleep(120);

	/* Color mode: RGB565 */
	rc = hw_display_write_cmd(ST7789_COLMOD);
	if (rc != 0) {
		return rc;
	}
	uint8_t colmod = 0x55;
	rc = hw_display_write_data(&colmod, 1);
	if (rc != 0) {
		return rc;
	}

	/* Memory access control: native orientation */
	rc = hw_display_write_cmd(ST7789_MADCTL);
	if (rc != 0) {
		return rc;
	}
	uint8_t madctl = 0x00;
	rc = hw_display_write_data(&madctl, 1);
	if (rc != 0) {
		return rc;
	}

	/* Display on */
	rc = hw_display_write_cmd(ST7789_DISPON);
	if (rc != 0) {
		return rc;
	}

	return 0;
}

int display_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	int rc;
	uint8_t data[4];

	/* Column address set */
	rc = hw_display_write_cmd(ST7789_CASET);
	if (rc != 0) {
		return rc;
	}
	data[0] = x0 >> 8;
	data[1] = x0 & 0xFF;
	data[2] = x1 >> 8;
	data[3] = x1 & 0xFF;
	rc = hw_display_write_data(data, 4);
	if (rc != 0) {
		return rc;
	}

	/* Row address set */
	rc = hw_display_write_cmd(ST7789_RASET);
	if (rc != 0) {
		return rc;
	}
	data[0] = y0 >> 8;
	data[1] = y0 & 0xFF;
	data[2] = y1 >> 8;
	data[3] = y1 & 0xFF;
	rc = hw_display_write_data(data, 4);
	if (rc != 0) {
		return rc;
	}

	/* Memory write */
	rc = hw_display_write_cmd(ST7789_RAMWR);
	return rc;
}

int display_clear(uint16_t color)
{
	int rc = display_set_window(0, 0, DISPLAY_WIDTH - 1,
				    DISPLAY_HEIGHT - 1);
	if (rc != 0) {
		return rc;
	}

	/* Send pixel data in chunks */
	uint8_t row_buf[DISPLAY_WIDTH * 2];
	for (int i = 0; i < DISPLAY_WIDTH; i++) {
		row_buf[i * 2] = color >> 8;
		row_buf[i * 2 + 1] = color & 0xFF;
	}

	for (int y = 0; y < DISPLAY_HEIGHT; y++) {
		rc = hw_display_write_pixels(row_buf, sizeof(row_buf));
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}

int display_draw_string(int x, int y, const char *str, uint16_t fg,
			uint16_t bg)
{
	int rc;
	uint8_t pixel_buf[FONT_WIDTH * 2]; /* One row of one character */

	for (int ch_idx = 0; str[ch_idx] != '\0'; ch_idx++) {
		const uint8_t *glyph = font_get_char(str[ch_idx]);
		int cx = x + ch_idx * FONT_WIDTH;

		if (cx + FONT_WIDTH > DISPLAY_WIDTH || cx < 0) {
			continue;
		}
		if (y + FONT_HEIGHT > DISPLAY_HEIGHT || y < 0) {
			continue;
		}

		rc = display_set_window((uint16_t)cx, (uint16_t)y,
					(uint16_t)(cx + FONT_WIDTH - 1),
					(uint16_t)(y + FONT_HEIGHT - 1));
		if (rc != 0) {
			return rc;
		}

		for (int row = 0; row < FONT_HEIGHT; row++) {
			uint8_t bits = glyph[row];
			for (int col = 0; col < FONT_WIDTH; col++) {
				uint16_t color = (bits & (0x80 >> col)) ?
						 fg : bg;
				pixel_buf[col * 2] = color >> 8;
				pixel_buf[col * 2 + 1] = color & 0xFF;
			}
			rc = hw_display_write_pixels(pixel_buf,
						     sizeof(pixel_buf));
			if (rc != 0) {
				return rc;
			}
		}
	}

	return 0;
}

int display_payment_screen(const char *plate, uint32_t amount_centavos)
{
	int rc;

	/* Clear screen to black */
	rc = display_clear(COLOR_BLACK);
	if (rc != 0) {
		return rc;
	}

	/* Line 1: Plate (centered roughly) */
	int plate_x = (DISPLAY_WIDTH - (int)strlen(plate) * FONT_WIDTH) / 2;
	if (plate_x < 0) {
		plate_x = 0;
	}
	rc = display_draw_string(plate_x, 40, plate, COLOR_WHITE, COLOR_BLACK);
	if (rc != 0) {
		return rc;
	}

	/* Line 2: "BRL <reais>.<centavos>" */
	uint32_t reais = amount_centavos / 100;
	uint32_t cents = amount_centavos % 100;
	char amount_str[32];
	snprintf(amount_str, sizeof(amount_str), "BRL %u.%02u", reais, cents);

	int amount_x = (DISPLAY_WIDTH - (int)strlen(amount_str) * FONT_WIDTH) / 2;
	if (amount_x < 0) {
		amount_x = 0;
	}
	rc = display_draw_string(amount_x, 100, amount_str, COLOR_GREEN,
				 COLOR_BLACK);
	if (rc != 0) {
		return rc;
	}

	/* Line 3: "SIMULATED PAYMENT" */
	const char *sim_text = "SIMULATED PAYMENT";
	int sim_x = (DISPLAY_WIDTH - (int)strlen(sim_text) * FONT_WIDTH) / 2;
	if (sim_x < 0) {
		sim_x = 0;
	}
	rc = display_draw_string(sim_x, 160, sim_text, COLOR_YELLOW,
				 COLOR_BLACK);
	if (rc != 0) {
		return rc;
	}

	/* Line 4: "NOT PIX" */
	const char *pix_text = "NOT PIX";
	int pix_x = (DISPLAY_WIDTH - (int)strlen(pix_text) * FONT_WIDTH) / 2;
	if (pix_x < 0) {
		pix_x = 0;
	}
	rc = display_draw_string(pix_x, 220, pix_text, COLOR_WHITE,
				 COLOR_BLACK);
	if (rc != 0) {
		return rc;
	}

	return 0;
}
