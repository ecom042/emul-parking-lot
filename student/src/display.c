/*
 * Display ST7789V (REQ-12). Ver display.h.
 */
#include "display.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "font.h"
#include "hw.h"
#include "tariff.h"

/* Comandos do subconjunto suportado. */
#define CMD_SWRESET 0x01
#define CMD_SLPOUT 0x11
#define CMD_CASET 0x2A
#define CMD_RASET 0x2B
#define CMD_RAMWR 0x2C
#define CMD_MADCTL 0x36
#define CMD_COLMOD 0x3A
#define CMD_DISPON 0x29

/* RGB565 com o byte alto primeiro. */
#define COLOR_FG 0xFFFFU /* branco */
#define COLOR_BG 0x0000U /* preto */

/* Fonte ampliada 2x numa célula de 12x16 (1 coluna e 1 linha de espaço). */
#define SCALE 2
#define CELL_W ((FONT_W + 1) * SCALE)
#define CELL_H ((FONT_H + 1) * SCALE)

/* Posição das linhas da tela de cobrança. */
#define TEXT_X 12
#define LINE_PLATE_Y 40
#define LINE_AMOUNT_Y 100
#define LINE_STATUS1_Y 180
#define LINE_STATUS2_Y 220

/* Campos de largura fixa: um texto novo apaga o anterior. */
#define PLATE_FIELD 7
#define AMOUNT_FIELD 14

/* Pixels de uma célula; estático para não pesar na pilha. */
static uint8_t cell_buf[CELL_W * CELL_H * 2];

static int command(uint8_t cmd, uint8_t *params, size_t len)
{
	int rc = hw_display_write(false, &cmd, 1);

	if (rc == 0 && len > 0) {
		rc = hw_display_write(true, params, len);
	}
	return rc;
}

/* Janela [x0..x1] x [y0..y1] para o próximo RAMWR. */
static int set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	uint8_t cols[4] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
	uint8_t rows[4] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};
	int rc = command(CMD_CASET, cols, sizeof(cols));

	if (rc == 0) {
		rc = command(CMD_RASET, rows, sizeof(rows));
	}
	return rc;
}

int display_init(void)
{
	/* Não são const: o driver SPI recebe ponteiros não constantes. */
	static uint8_t colmod[] = {0x55}; /* RGB565 */
	static uint8_t madctl[] = {0x00}; /* orientação nativa */
	int rc;

	/* Reset por hardware: 10 ms em reset, 120 ms para acordar. */
	rc = hw_display_reset(true);
	if (rc != 0) {
		return rc;
	}
	hw_sleep_ms(10);
	rc = hw_display_reset(false);
	if (rc != 0) {
		return rc;
	}
	hw_sleep_ms(120);

	rc = command(CMD_SWRESET, NULL, 0);
	if (rc != 0) {
		return rc;
	}
	hw_sleep_ms(150);
	rc = command(CMD_SLPOUT, NULL, 0);
	if (rc != 0) {
		return rc;
	}
	hw_sleep_ms(120);
	rc = command(CMD_COLMOD, colmod, sizeof(colmod));
	if (rc == 0) {
		rc = command(CMD_MADCTL, madctl, sizeof(madctl));
	}
	if (rc == 0) {
		rc = command(CMD_DISPON, NULL, 0);
	}
	return rc;
}

/* Desenha um caractere na célula com canto superior esquerdo (x, y). */
static int draw_char(uint16_t x, uint16_t y, char c)
{
	const uint8_t *glyph = font_glyph(c);
	size_t i = 0;
	int rc;

	for (int py = 0; py < CELL_H; py++) {
		int row = py / SCALE;

		for (int px = 0; px < CELL_W; px++) {
			int col = px / SCALE;
			bool on = row < FONT_H && col < FONT_W &&
				  (glyph[row] & (0x10U >> col)) != 0U;
			uint16_t color = on ? COLOR_FG : COLOR_BG;

			cell_buf[i++] = (uint8_t)(color >> 8);
			cell_buf[i++] = (uint8_t)(color & 0xFF);
		}
	}

	rc = set_window(x, y, x + CELL_W - 1, y + CELL_H - 1);
	if (rc == 0) {
		rc = command(CMD_RAMWR, cell_buf, sizeof(cell_buf));
	}
	return rc;
}

int display_text(uint16_t x, uint16_t y, const char *text)
{
	for (; *text != '\0'; text++) {
		int rc;

		if (x + CELL_W > DISPLAY_W || y + CELL_H > DISPLAY_H) {
			return -EINVAL;
		}
		rc = draw_char(x, y, *text);
		if (rc != 0) {
			return rc;
		}
		x += CELL_W;
	}
	return 0;
}

/* Copia text para field, completando com espaços até width. */
static void pad(char *field, size_t width, const char *text)
{
	size_t n = strlen(text);

	if (n > width) {
		n = width;
	}
	memcpy(field, text, n);
	memset(field + n, ' ', width - n);
	field[width] = '\0';
}

int display_show_plate(const char *plate)
{
	char field[PLATE_FIELD + 1];

	pad(field, PLATE_FIELD, plate);
	return display_text(TEXT_X, LINE_PLATE_Y, field);
}

int display_show_amount(uint32_t centavos)
{
	char brl[AMOUNT_FIELD + 1];
	char field[AMOUNT_FIELD + 1];
	int rc;

	if (tariff_format_brl(brl, sizeof(brl), centavos) < 0) {
		return -EINVAL;
	}
	pad(field, AMOUNT_FIELD, brl);
	rc = display_text(TEXT_X, LINE_AMOUNT_Y, field);
	if (rc == 0) {
		rc = display_text(TEXT_X, LINE_STATUS1_Y, "SIMULATED PAYMENT");
	}
	if (rc == 0) {
		rc = display_text(TEXT_X, LINE_STATUS2_Y, "NOT PIX");
	}
	return rc;
}
