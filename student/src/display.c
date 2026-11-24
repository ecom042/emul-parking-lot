/* Desenho no ST7789V via SPI. Veja display.h. */
#include "display.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "hw.h"

/* Posições das linhas da tela de cobrança. */
#define Y_PLATE 60
#define Y_AMOUNT 120
#define Y_SIMULATED 180
#define Y_NOT_PIX 210

/** Uma linha de pixels RGB565, 2 bytes por pixel, byte alto primeiro. */
static uint8_t row[DISPLAY_WIDTH * 2];

/** Envia um comando (D/C baixo) e seus parâmetros (D/C alto). */
static int command(uint8_t cmd, uint8_t *params, size_t n)
{
	int rc = hw_disp_write(false, &cmd, 1);

	if (rc == 0 && n > 0U) {
		rc = hw_disp_write(true, params, n);
	}
	return rc;
}

/** CASET/RASET/RAMWR: seleciona a janela [x0..x1] x [y0..y1] e começa a escrita. */
static int window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	uint8_t cols[] = {x0 >> 8, x0 & 0xff, x1 >> 8, x1 & 0xff};
	uint8_t rows[] = {y0 >> 8, y0 & 0xff, y1 >> 8, y1 & 0xff};
	int rc = command(ST7789_CASET, cols, sizeof(cols));

	if (rc == 0) {
		rc = command(ST7789_RASET, rows, sizeof(rows));
	}
	if (rc == 0) {
		rc = command(ST7789_RAMWR, NULL, 0);
	}
	return rc;
}

static void put_pixel(uint16_t x, uint16_t color)
{
	row[2U * x] = (uint8_t)(color >> 8);
	row[2U * x + 1U] = (uint8_t)(color & 0xff);
}

int display_init(void)
{
	uint8_t colmod = 0x55; /* RGB565 */
	uint8_t madctl = 0x00; /* orientação nativa */
	int rc;

	/* Reset por hardware: 10 ms ativo, 120 ms para acordar. */
	rc = hw_disp_reset(true);
	hw_sleep_ms(10);
	if (rc == 0) {
		rc = hw_disp_reset(false);
	}
	hw_sleep_ms(120);
	if (rc == 0) {
		rc = command(ST7789_SWRESET, NULL, 0);
	}
	hw_sleep_ms(150);
	if (rc == 0) {
		rc = command(ST7789_SLPOUT, NULL, 0);
	}
	hw_sleep_ms(120);
	if (rc == 0) {
		rc = command(ST7789_COLMOD, &colmod, 1);
	}
	if (rc == 0) {
		rc = command(ST7789_MADCTL, &madctl, 1);
	}
	if (rc == 0) {
		rc = command(ST7789_DISPON, NULL, 0);
	}
	/* Tela inteira preta. */
	if (rc == 0) {
		rc = window(0, 0, DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1);
	}
	memset(row, 0, sizeof(row));
	for (int y = 0; rc == 0 && y < DISPLAY_HEIGHT; y++) {
		rc = hw_disp_write(true, row, sizeof(row));
	}
	return rc;
}

int display_text_band(uint16_t y, uint8_t scale, const char *text, uint16_t color)
{
	const size_t len = strlen(text);
	const uint16_t cell = (FONT_WIDTH + 1U) * scale; /* glifo + 1 coluna de espaço */
	const uint16_t height = 8U * scale;              /* 7 linhas + 1 de folga */
	size_t max_chars = DISPLAY_WIDTH / cell;
	size_t n = (len < max_chars) ? len : max_chars;  /* corta o que não cabe */
	uint16_t x0 = (uint16_t)((DISPLAY_WIDTH - n * cell) / 2U); /* centraliza */
	int rc = window(0, y, DISPLAY_WIDTH - 1, y + height - 1);

	for (uint16_t r = 0; rc == 0 && r < height; r++) {
		uint16_t glyph_row = r / scale; /* linha da fonte (7 = folga) */

		memset(row, 0, sizeof(row)); /* fundo preto */
		for (size_t i = 0; glyph_row < FONT_HEIGHT && i < n; i++) {
			uint8_t bits = font_glyph(text[i])[glyph_row];

			for (uint16_t c = 0; c < FONT_WIDTH; c++) {
				if ((bits & (0x10U >> c)) == 0U) {
					continue;
				}
				for (uint16_t k = 0; k < scale; k++) {
					put_pixel(x0 + i * cell + c * scale + k, color);
				}
			}
		}
		rc = hw_disp_write(true, row, sizeof(row));
	}
	return rc;
}

int display_format_amount(char *buf, size_t size, uint32_t centavos)
{
	int n = snprintf(buf, size, "BRL %u.%02u", (unsigned int)(centavos / 100U),
			 (unsigned int)(centavos % 100U));

	/* n negativo (erro) vira um size_t enorme e também cai em -1. */
	return ((size_t)n < size) ? n : -1;
}

int display_show_plate(const char *plate)
{
	int rc = display_text_band(Y_PLATE, 3, plate, COLOR_WHITE);

	if (rc == 0) {
		rc = display_text_band(Y_AMOUNT, 3, "", COLOR_WHITE);
	}
	if (rc == 0) {
		rc = display_text_band(Y_SIMULATED, 2, "", COLOR_WHITE);
	}
	if (rc == 0) {
		rc = display_text_band(Y_NOT_PIX, 2, "", COLOR_WHITE);
	}
	return rc;
}

int display_show_amount(uint32_t centavos)
{
	/* "BRL 10000.00" (o maior valor) tem 12 caracteres: sempre cabe. */
	char text[24];
	int rc;

	(void)display_format_amount(text, sizeof(text), centavos);
	rc = display_text_band(Y_AMOUNT, 3, text, COLOR_YELLOW);
	if (rc == 0) {
		rc = display_text_band(Y_SIMULATED, 2, "SIMULATED PAYMENT", COLOR_WHITE);
	}
	if (rc == 0) {
		rc = display_text_band(Y_NOT_PIX, 2, "NOT PIX", COLOR_WHITE);
	}
	return rc;
}
