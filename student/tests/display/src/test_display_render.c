/*
 * test_display_render.c — fonte, formatação do valor, layout e bytes RGB565 (REQ-12).
 * Lógica pura: nenhum falso necessário.
 */
#include <string.h>

#include <zephyr/ztest.h>

#include "display_render.h"
#include "font5x7.h"

ZTEST_SUITE(display_render, NULL, NULL, NULL, NULL, NULL);

/* ---- fonte ---- */

ZTEST(display_render, test_font_has_every_character_the_screens_need)
{
	static const char needed[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789. ";

	for (size_t i = 0U; needed[i] != '\0'; ++i) {
		zassert_true(font_has_glyph(needed[i]), "falta o glifo %c", needed[i]);
		zassert_equal(font_lookup(needed[i])->character, needed[i]);
	}
}

ZTEST(display_render, test_glyph_shape_and_unknown_characters_are_blank)
{
	const struct font_glyph *t = font_lookup('T');

	/* T: barra de cima inteira e haste no meio */
	for (unsigned int col = 0U; col < FONT_COLS; ++col) {
		zassert_true(font_pixel(t, col, 0U));
	}
	zassert_true(font_pixel(t, 2U, 6U));
	zassert_false(font_pixel(t, 0U, 3U));
	/* fora da grade 5x7 nunca acende */
	zassert_false(font_pixel(t, FONT_COLS, 0U));
	zassert_false(font_pixel(t, 0U, FONT_ROWS));

	/* caractere desconhecido => glifo em branco, nunca NULL */
	zassert_false(font_has_glyph('@'));
	for (unsigned int row = 0U; row < FONT_ROWS; ++row) {
		for (unsigned int col = 0U; col < FONT_COLS; ++col) {
			zassert_false(font_pixel(font_lookup('@'), col, row));
		}
	}
}

/* ---- valor em reais ---- */

ZTEST(display_render, test_amount_text_formats_reais_and_centavos)
{
	char text[DISPLAY_AMOUNT_CHARS + 1U];

	zassert_true(display_amount_text(400U, text));
	zassert_str_equal(text, "BRL 4.00    "); /* exemplo do desafio: 2,5 s = BRL 4.00 */
	zassert_true(display_amount_text(200U, text));
	zassert_str_equal(text, "BRL 2.00    ");
	zassert_true(display_amount_text(5U, text));
	zassert_str_equal(text, "BRL 0.05    ");
	zassert_true(display_amount_text(12345U, text));
	zassert_str_equal(text, "BRL 123.45  ");
	zassert_true(display_amount_text(1000000U, text)); /* o máximo: 9999 unidades */
	zassert_str_equal(text, "BRL 10000.00");
	zassert_equal(strlen(text), DISPLAY_AMOUNT_CHARS);
	zassert_false(display_amount_text(1000001U, text));
}

/* ---- bytes de pixel ---- */

ZTEST(display_render, test_render_row_bytes_are_rgb565_high_byte_first_and_scaled)
{
	const struct display_item item = {
		.text = "A", .x = 0U, .y = 0U, .chars = 1U, .scale = 2U,
		.fg = DISPLAY_COLOR_RED, .bg = DISPLAY_COLOR_BLACK,
	};
	uint8_t row0[DISPLAY_ROW_BYTES_MAX];
	uint8_t row1[DISPLAY_ROW_BYTES_MAX];
	uint8_t row2[DISPLAY_ROW_BYTES_MAX];

	zassert_equal(display_item_width(&item), 12U);  /* 1 letra * 6 colunas * escala 2 */
	zassert_equal(display_item_height(&item), 14U); /* 7 linhas * escala 2 */
	zassert_equal(display_render_row(&item, 0U, row0), 24U);
	/* linha 0 de 'A' é ".###.": 2 px apagados, 6 px acesos (F800), 2 apagados, + 2 de espaço */
	static const uint8_t expected0[24] = {
		0x00, 0x00, 0x00, 0x00,
		0xF8, 0x00, 0xF8, 0x00, 0xF8, 0x00, 0xF8, 0x00, 0xF8, 0x00, 0xF8, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	zassert_mem_equal(row0, expected0, sizeof(expected0));
	/* a escala repete cada linha da letra: linhas 0 e 1 são iguais, a 2 já é a linha 1 da letra */
	zassert_equal(display_render_row(&item, 1U, row1), 24U);
	zassert_mem_equal(row0, row1, 24U);
	zassert_equal(display_render_row(&item, 2U, row2), 24U);
	zassert_true(memcmp(row0, row2, 24U) != 0);
	/* fora do retângulo: nada */
	zassert_equal(display_render_row(&item, 14U, row0), 0U);
}

ZTEST(display_render, test_short_text_is_padded_with_blank_cells)
{
	const struct display_item item = {
		.text = "A", .chars = 3U, .scale = 1U, .fg = DISPLAY_COLOR_WHITE, .bg = DISPLAY_COLOR_BLACK,
	};
	uint8_t row[DISPLAY_ROW_BYTES_MAX];

	zassert_equal(display_render_row(&item, 3U, row), 36U); /* 3 letras * 6 px * 2 bytes */
	for (size_t i = 12U; i < 36U; ++i) {
		zassert_equal(row[i], 0U); /* 2ª e 3ª células: espaços */
	}
}

/* ---- layout ---- */

static void assert_inside_screen(const struct display_item *item)
{
	zassert_true(item->x + display_item_width(item) <= DISPLAY_WIDTH);
	zassert_true(item->y + display_item_height(item) <= DISPLAY_HEIGHT);
}

ZTEST(display_render, test_charge_layout_has_the_four_required_texts_inside_the_screen)
{
	struct display_item items[DISPLAY_MAX_ITEMS];

	zassert_equal(display_layout_charge("ABC1D23", 400U, items), 4U);
	zassert_str_equal(items[0].text, "ABC1D23");
	zassert_str_equal(items[1].text, "BRL 4.00    ");
	zassert_str_equal(items[2].text, "SIMULATED PAYMENT");
	zassert_str_equal(items[3].text, "NOT PIX");
	for (size_t i = 0U; i < 4U; ++i) {
		assert_inside_screen(&items[i]);
		zassert_equal(items[i].x, (DISPLAY_WIDTH - display_item_width(&items[i])) / 2U);
		if (i > 0U) { /* uma linha não invade a de cima */
			zassert_true(items[i].y >= items[i - 1U].y + display_item_height(&items[i - 1U]));
		}
	}
}

ZTEST(display_render, test_plate_layout_also_blanks_the_previous_amount)
{
	struct display_item items[DISPLAY_MAX_ITEMS];
	struct display_item charge[DISPLAY_MAX_ITEMS];

	zassert_equal(display_layout_plate("ABC1D23", items), 2U);
	zassert_str_equal(items[0].text, "ABC1D23");
	zassert_str_equal(items[1].text, ""); /* retângulo em branco */
	assert_inside_screen(&items[0]);
	assert_inside_screen(&items[1]);
	/* o retângulo apagado é EXATAMENTE o do valor da tela de cobrança */
	zassert_equal(display_layout_charge("ABC1D23", 400U, charge), 4U);
	zassert_equal(items[1].x, charge[1].x);
	zassert_equal(items[1].y, charge[1].y);
	zassert_equal(items[1].chars, charge[1].chars);
	zassert_equal(items[1].scale, charge[1].scale);
}

ZTEST(display_render, test_layout_rejects_bad_arguments_and_truncates_long_plates)
{
	struct display_item items[DISPLAY_MAX_ITEMS];

	zassert_equal(display_layout_plate(NULL, items), 0U);
	zassert_equal(display_layout_charge(NULL, 400U, items), 0U);
	zassert_equal(display_layout_charge("ABC1D23", 1000001U, items), 0U);
	zassert_equal(display_layout_plate("ABCDEFGHIJ", items), 2U); /* sem '\0' nos 7 primeiros */
	zassert_str_equal(items[0].text, "ABCDEFG");
	zassert_equal(display_layout_plate("AB", items), 2U);
	zassert_str_equal(items[0].text, "AB");
}
