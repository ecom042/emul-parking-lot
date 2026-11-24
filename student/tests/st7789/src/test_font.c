/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Fonte 5x7 (font.c): os caracteres da tabela tem glifo, os outros nao.
 */
#include <string.h>

#include <zephyr/ztest.h>

#include "font.h"

static const char tabela[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-";

ZTEST_SUITE(font, NULL, NULL, NULL, NULL, NULL);

static void confere_glifo(char c, const uint8_t esperado[FONT_COLS])
{
	const uint8_t *g = font_glyph(c);

	zassert_not_null(g, "'%c' sem glifo", c);
	zassert_mem_equal(g, esperado, FONT_COLS, "glifo de '%c' errado", c);
}

ZTEST(font, test_glifos_conferidos_a_mao)
{
	static const uint8_t espaco[FONT_COLS] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
	static const uint8_t a[FONT_COLS] = { 0x7e, 0x09, 0x09, 0x09, 0x7e };
	static const uint8_t z[FONT_COLS] = { 0x61, 0x51, 0x49, 0x45, 0x43 };
	static const uint8_t zero[FONT_COLS] = { 0x3e, 0x51, 0x49, 0x45, 0x3e };
	static const uint8_t nove[FONT_COLS] = { 0x06, 0x49, 0x49, 0x29, 0x1e };
	static const uint8_t ponto[FONT_COLS] = { 0x00, 0x00, 0x60, 0x60, 0x00 };
	static const uint8_t hifen[FONT_COLS] = { 0x00, 0x08, 0x08, 0x08, 0x00 };

	confere_glifo(' ', espaco);
	confere_glifo('A', a);
	confere_glifo('Z', z);
	confere_glifo('0', zero);
	confere_glifo('9', nove);
	confere_glifo('.', ponto);
	confere_glifo('-', hifen);
}

ZTEST(font, test_toda_a_tabela_tem_glifo_proprio_de_sete_linhas)
{
	for (size_t i = 0; i < sizeof(tabela) - 1; i++) {
		const uint8_t *g = font_glyph(tabela[i]);

		zassert_not_null(g, "'%c' sem glifo", tabela[i]);
		for (int col = 0; col < FONT_COLS; col++) {
			zassert_equal(g[col] & 0x80, 0, "'%c' usa a oitava linha",
				      tabela[i]);
		}
		/* Dois caracteres diferentes nunca tem o mesmo desenho. */
		for (size_t k = 0; k < i; k++) {
			zassert_true(memcmp(g, font_glyph(tabela[k]), FONT_COLS) != 0,
				     "'%c' e '%c' iguais", tabela[i], tabela[k]);
		}
	}
}

ZTEST(font, test_caracteres_fora_da_tabela_nao_tem_glifo)
{
	/* Vizinhos de cada faixa da tabela, minusculas, controle e bytes com o
	 * bit 7 ligado (char negativo).
	 */
	static const char fora[] = {
		'\0', '\n', '!', '+', ',', '/', ':', '@', '[', '`',
		'a', 'z', '{', '\x7f', (char)0x80, (char)0xc3, (char)0xff,
	};

	for (size_t i = 0; i < sizeof(fora); i++) {
		zassert_is_null(font_glyph(fora[i]), "0x%02x tem glifo",
				(unsigned int)(uint8_t)fora[i]);
	}
}
