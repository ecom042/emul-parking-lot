/*
 * display.c — Non-blocking ST7789V SPI controller and payment screen renderer
 *
 * Performs hardware reset transition (inactive -> active 10 ms -> inactive
 * 120 ms), SWRESET (150 ms), SLPOUT (120 ms), COLMOD=0x55 (RGB565),
 * MADCTL=0x00, DISPON without blocking the main safety loop.
 * Renders centered full-width bands via CASET/RASET/RAMWR.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "display.h"
#include "font.h"
#include "hw.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define ST7789_SWRESET 0x01U
#define ST7789_SLPOUT  0x11U
#define ST7789_DISPON  0x29U
#define ST7789_CASET   0x2AU
#define ST7789_RASET   0x2BU
#define ST7789_RAMWR   0x2CU
#define ST7789_MADCTL  0x36U
#define ST7789_COLMOD  0x3AU

#define RGB565_BLACK   0x0000U
#define RGB565_WHITE   0xFFFFU
#define RGB565_GREEN   0x07E0U
#define RGB565_YELLOW  0xFFE0U
#define RGB565_CYAN    0x07FFU

static display_yield_fn draw_yield;

void display_set_yield(display_yield_fn fn)
{
	draw_yield = fn;
}

static void yield_to_controller(void)
{
	if (draw_yield != NULL) {
		draw_yield();
	}
}

static int write_cmd(uint8_t cmd)
{
	uint8_t buf[1] = { cmd };
	int rc = hw_out_set(HW_OUT_DISPLAY_DC, false);

	if (rc != 0) {
		return rc;
	}
	return hw_spi_write(buf, sizeof(buf));
}

static int write_data(uint8_t *data, size_t len)
{
	int rc = hw_out_set(HW_OUT_DISPLAY_DC, true);

	if (rc != 0) {
		return rc;
	}
	return hw_spi_write(data, len);
}

static int set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	uint8_t coords[4];
	int rc;

	rc = write_cmd(ST7789_CASET);
	if (rc != 0) {
		return rc;
	}
	coords[0] = (uint8_t)(x0 >> 8);
	coords[1] = (uint8_t)(x0 & 0xFFU);
	coords[2] = (uint8_t)(x1 >> 8);
	coords[3] = (uint8_t)(x1 & 0xFFU);
	rc = write_data(coords, sizeof(coords));
	if (rc != 0) {
		return rc;
	}

	rc = write_cmd(ST7789_RASET);
	if (rc != 0) {
		return rc;
	}
	coords[0] = (uint8_t)(y0 >> 8);
	coords[1] = (uint8_t)(y0 & 0xFFU);
	coords[2] = (uint8_t)(y1 >> 8);
	coords[3] = (uint8_t)(y1 & 0xFFU);
	rc = write_data(coords, sizeof(coords));
	if (rc != 0) {
		return rc;
	}

	return write_cmd(ST7789_RAMWR);
}

static int draw_centered_band(uint16_t y_top, const char *text, uint8_t scale,
			      uint16_t fg, uint16_t bg)
{
	uint8_t row_buf[DISPLAY_WIDTH * 2U];
	size_t len = strlen(text);
	uint16_t char_w = (uint16_t)(FONT_WIDTH * (uint16_t)scale);
	uint16_t band_h = (uint16_t)(FONT_HEIGHT * (uint16_t)scale);
	uint16_t text_w = (uint16_t)(len * (size_t)char_w);
	uint16_t x_start = (text_w < DISPLAY_WIDTH) ?
			   (uint16_t)((DISPLAY_WIDTH - text_w) / 2U) : 0U;
	int rc;

	rc = set_window(0U, y_top, (uint16_t)(DISPLAY_WIDTH - 1U),
			(uint16_t)(y_top + band_h - 1U));
	if (rc != 0) {
		return rc;
	}

	for (uint16_t row = 0U; row < band_h; row++) {
		uint16_t glyph_row = row / (uint16_t)scale;

		/* A full frame is too long for the ADC deadline under coverage builds. */
		yield_to_controller();

		for (uint16_t x = 0U; x < DISPLAY_WIDTH; x++) {
			uint16_t color = bg;

			if (x >= x_start && (uint32_t)(x - x_start) < (uint32_t)text_w) {
				uint16_t rel_x = (uint16_t)(x - x_start);
				size_t idx = (size_t)(rel_x / char_w);
				uint16_t bit_col = (rel_x % char_w) / (uint16_t)scale;
				const uint8_t *glyph = font_get_char(text[idx]);

				if ((glyph[glyph_row] & (uint8_t)(0x80U >> bit_col)) != 0U) {
					color = fg;
				}
			}

			row_buf[x * 2U] = (uint8_t)(color >> 8);
			row_buf[(x * 2U) + 1U] = (uint8_t)(color & 0xFFU);
		}

		rc = write_data(row_buf, sizeof(row_buf));
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}

int display_start_init(display_ctx_t *ctx, int64_t now_ms)
{
	int rc;

	(void)memset(ctx, 0, sizeof(*ctx));
	ctx->state = DISP_STATE_UNINIT;

	if (!hw_spi_is_ready() ||
	    !hw_out_is_ready(HW_OUT_DISPLAY_DC) ||
	    !hw_out_is_ready(HW_OUT_DISPLAY_RESET)) {
		ctx->state = DISP_STATE_ERROR;
		return -ENODEV;
	}

	rc = hw_out_configure_inactive(HW_OUT_DISPLAY_DC);
	if (rc != 0) {
		ctx->state = DISP_STATE_ERROR;
		return rc;
	}

	/* Configure reset inactive first (physical HIGH), then assert active (physical LOW) */
	rc = hw_out_configure_inactive(HW_OUT_DISPLAY_RESET);
	if (rc != 0) {
		ctx->state = DISP_STATE_ERROR;
		return rc;
	}

	rc = hw_out_set(HW_OUT_DISPLAY_RESET, true);
	if (rc != 0) {
		ctx->state = DISP_STATE_ERROR;
		return rc;
	}

	ctx->state = DISP_STATE_RESET_ASSERT;
	ctx->step_ms = now_ms;
	return 0;
}

void display_service(display_ctx_t *ctx, int64_t now_ms)
{
	int rc;
	uint8_t param[1];

	switch (ctx->state) {
	case DISP_STATE_UNINIT:
	case DISP_STATE_READY:
	case DISP_STATE_ERROR:
		return;

	case DISP_STATE_RESET_ASSERT:
		if ((now_ms - ctx->step_ms) < DISPLAY_RESET_ASSERT_MS) {
			return;
		}
		rc = hw_out_set(HW_OUT_DISPLAY_RESET, false);
		if (rc != 0) {
			ctx->state = DISP_STATE_ERROR;
			return;
		}
		ctx->state = DISP_STATE_RESET_WAKE;
		ctx->step_ms = now_ms;
		return;

	case DISP_STATE_RESET_WAKE:
		if ((now_ms - ctx->step_ms) < DISPLAY_RESET_WAKE_MS) {
			return;
		}
		rc = write_cmd(ST7789_SWRESET);
		if (rc != 0) {
			ctx->state = DISP_STATE_ERROR;
			return;
		}
		ctx->state = DISP_STATE_SWRESET_WAIT;
		ctx->step_ms = now_ms;
		return;

	case DISP_STATE_SWRESET_WAIT:
		if ((now_ms - ctx->step_ms) < DISPLAY_SWRESET_WAIT_MS) {
			return;
		}
		rc = write_cmd(ST7789_SLPOUT);
		if (rc != 0) {
			ctx->state = DISP_STATE_ERROR;
			return;
		}
		ctx->state = DISP_STATE_SLPOUT_WAIT;
		ctx->step_ms = now_ms;
		return;

	case DISP_STATE_SLPOUT_WAIT:
		if ((now_ms - ctx->step_ms) < DISPLAY_SLPOUT_WAIT_MS) {
			return;
		}
		rc = write_cmd(ST7789_COLMOD);
		if (rc != 0) {
			ctx->state = DISP_STATE_ERROR;
			return;
		}
		param[0] = 0x55U;
		rc = write_data(param, 1U);
		if (rc != 0) {
			ctx->state = DISP_STATE_ERROR;
			return;
		}
		rc = write_cmd(ST7789_MADCTL);
		if (rc != 0) {
			ctx->state = DISP_STATE_ERROR;
			return;
		}
		param[0] = 0x00U;
		rc = write_data(param, 1U);
		if (rc != 0) {
			ctx->state = DISP_STATE_ERROR;
			return;
		}
		rc = write_cmd(ST7789_DISPON);
		if (rc != 0) {
			ctx->state = DISP_STATE_ERROR;
			return;
		}
		ctx->state = DISP_STATE_READY;
		return;
	}
}

bool display_is_ready(const display_ctx_t *ctx)
{
	return ctx->state == DISP_STATE_READY;
}

bool display_has_error(const display_ctx_t *ctx)
{
	return ctx->state == DISP_STATE_ERROR;
}

int display_render_plate(display_ctx_t *ctx, const char *plate)
{
	int rc;

	if (ctx->state != DISP_STATE_READY) {
		return -EBUSY;
	}

	rc = draw_centered_band(48U, plate, 2U, RGB565_WHITE, RGB565_BLACK);
	if (rc != 0) {
		ctx->state = DISP_STATE_ERROR;
		return rc;
	}
	return 0;
}

int display_render_charge_details(display_ctx_t *ctx, uint32_t amount_centavos)
{
	char amount_str[24];
	uint32_t reais = amount_centavos / 100U;
	uint32_t cents = amount_centavos % 100U;
	int rc;

	if (ctx->state != DISP_STATE_READY) {
		return -EBUSY;
	}

	(void)snprintf(amount_str, sizeof(amount_str),
		       "BRL %" PRIu32 ".%02" PRIu32, reais, cents);

	rc = draw_centered_band(116U, amount_str, 2U, RGB565_GREEN, RGB565_BLACK);
	if (rc != 0) {
		ctx->state = DISP_STATE_ERROR;
		return rc;
	}

	rc = draw_centered_band(192U, "SIMULATED PAYMENT", 1U, RGB565_YELLOW,
				RGB565_BLACK);
	if (rc != 0) {
		ctx->state = DISP_STATE_ERROR;
		return rc;
	}

	rc = draw_centered_band(232U, "NOT PIX", 2U, RGB565_CYAN, RGB565_BLACK);
	if (rc != 0) {
		ctx->state = DISP_STATE_ERROR;
		return rc;
	}

	return 0;
}
