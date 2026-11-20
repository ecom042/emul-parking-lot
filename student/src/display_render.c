/*
 * display_render.c — layout e renderização. Conceitos: veja display_render.h.
 *
 * GEOMETRIA DA TELA DE COBRANÇA (240 x 320, centralizada na horizontal):
 *
 *      y= 20  ┌──────────────────────────┐  PLACA            (escala 4, amarelo)
 *             │       A B C 1 D 2 3      │  28 px de altura
 *      y= 90  │  B R L  4 . 0 0          │  VALOR            (escala 3, verde)
 *      y=170  │ SIMULATED PAYMENT        │  (escala 2, branco)
 *      y=210  │      N O T  P I X        │  (escala 3, vermelho)
 *             └──────────────────────────┘
 */
#include <string.h>

#include "display_render.h"

#define PLATE_Y 20U
#define PLATE_SCALE 4U
#define AMOUNT_Y 90U
#define AMOUNT_SCALE 3U
#define SIMULATED_Y 170U
#define SIMULATED_SCALE 2U
#define NOTPIX_Y 210U
#define NOTPIX_SCALE 3U

uint16_t display_item_width(const struct display_item *item)
{
	return (uint16_t)(item->chars * DISPLAY_CELL_COLS * item->scale);
}

uint16_t display_item_height(const struct display_item *item)
{
	return (uint16_t)(FONT_ROWS * item->scale);
}

size_t display_render_row(const struct display_item *item, unsigned int row, uint8_t *out)
{
	unsigned int glyph_row;
	size_t pos = 0U;

	if (row >= display_item_height(item)) {
		return 0U;
	}
	glyph_row = row / item->scale; /* cada linha da letra se repete `scale` vezes */
	for (unsigned int i = 0U; i < item->chars; ++i) {
		/* texto mais curto que `chars` é completado com espaços em branco */
		const struct font_glyph *glyph = font_lookup(i < strlen(item->text) ? item->text[i] : ' ');

		for (unsigned int col = 0U; col < DISPLAY_CELL_COLS; ++col) {
			/* a 6ª coluna (índice 5) é o espaço entre letras: font_pixel devolve false */
			uint16_t color = font_pixel(glyph, col, glyph_row) ? item->fg : item->bg;

			for (unsigned int repeat = 0U; repeat < item->scale; ++repeat) {
				out[pos++] = (uint8_t)(color >> 8); /* byte ALTO primeiro */
				out[pos++] = (uint8_t)(color & 0xFFU);
			}
		}
	}
	return pos;
}

bool display_amount_text(uint32_t centavos, char out[DISPLAY_AMOUNT_CHARS + 1U])
{
	char digits[8];
	size_t count = 0U;
	size_t pos = 0U;
	uint32_t reais = centavos / 100U;
	uint32_t cents = centavos % 100U;

	if (centavos > 1000000U) {
		return false; /* "BRL 10000.00" é o maior valor que cabe nos 12 caracteres */
	}
	memcpy(out, "BRL ", 4U);
	pos = 4U;
	do { /* reais, dígito a dígito, de trás para frente */
		digits[count++] = (char)('0' + reais % 10U);
		reais /= 10U;
	} while (reais > 0U);
	while (count > 0U) {
		out[pos++] = digits[--count];
	}
	out[pos++] = '.';
	out[pos++] = (char)('0' + cents / 10U);
	out[pos++] = (char)('0' + cents % 10U);
	while (pos < DISPLAY_AMOUNT_CHARS) { /* completa com espaços: largura fixa */
		out[pos++] = ' ';
	}
	out[pos] = '\0';
	return true;
}

/* Preenche um item. O texto é truncado ao tamanho do campo, sempre com '\0'. */
static void set_item(struct display_item *item, const char *text, size_t chars, unsigned int scale,
		     unsigned int y, uint16_t fg)
{
	memset(item, 0, sizeof(*item));
	strncpy(item->text, text, DISPLAY_TEXT_MAX - 1U);
	item->chars = (uint8_t)chars;
	item->scale = (uint8_t)scale;
	item->fg = fg;
	item->bg = DISPLAY_COLOR_BLACK;
	item->y = (uint16_t)y;
	item->x = (uint16_t)((DISPLAY_WIDTH - display_item_width(item)) / 2U); /* centraliza */
}

/* A placa vira um texto de, no máximo, 7 caracteres (o 8º byte do buffer é o '\0'). */
static void plate_text(const char *plate, char out[DISPLAY_PLATE_CHARS + 1U])
{
	size_t length = 0U;

	while (length < DISPLAY_PLATE_CHARS && plate[length] != '\0') {
		out[length] = plate[length];
		length++;
	}
	out[length] = '\0';
}

size_t display_layout_plate(const char *plate, struct display_item items[DISPLAY_MAX_ITEMS])
{
	char text[DISPLAY_PLATE_CHARS + 1U];

	if (plate == NULL) {
		return 0U;
	}
	plate_text(plate, text);
	set_item(&items[0], text, DISPLAY_PLATE_CHARS, PLATE_SCALE, PLATE_Y, DISPLAY_COLOR_YELLOW);
	/* o valor da saída anterior não pode aparecer junto da placa nova: apaga-o */
	set_item(&items[1], "", DISPLAY_AMOUNT_CHARS, AMOUNT_SCALE, AMOUNT_Y, DISPLAY_COLOR_GREEN);
	return 2U;
}

size_t display_layout_charge(const char *plate, uint32_t centavos,
			     struct display_item items[DISPLAY_MAX_ITEMS])
{
	char text[DISPLAY_PLATE_CHARS + 1U];
	char amount[DISPLAY_AMOUNT_CHARS + 1U];

	if (plate == NULL || !display_amount_text(centavos, amount)) {
		return 0U;
	}
	plate_text(plate, text);
	set_item(&items[0], text, DISPLAY_PLATE_CHARS, PLATE_SCALE, PLATE_Y, DISPLAY_COLOR_YELLOW);
	set_item(&items[1], amount, DISPLAY_AMOUNT_CHARS, AMOUNT_SCALE, AMOUNT_Y, DISPLAY_COLOR_GREEN);
	set_item(&items[2], "SIMULATED PAYMENT", 17U, SIMULATED_SCALE, SIMULATED_Y,
		 DISPLAY_COLOR_WHITE);
	set_item(&items[3], "NOT PIX", 7U, NOTPIX_SCALE, NOTPIX_Y, DISPLAY_COLOR_RED);
	return 4U;
}
