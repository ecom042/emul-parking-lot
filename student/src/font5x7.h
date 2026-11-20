/*
 * font5x7.h — Fonte de bitmap 5x7 (cada letra é uma grade de 5 colunas x 7 linhas)
 * ================================================================================
 *
 * PARA QUE SERVE?
 *   O display ST7789V não sabe desenhar texto: ele só recebe PIXELS. Então o
 *   firmware precisa saber "como é a letra A em pixels". Este módulo guarda isso.
 *   O desafio diz "com a fonte que quiser" — esta é a nossa.
 *
 * COMO AS LETRAS SÃO GUARDADAS (de propósito, legível a olho nu):
 *   Cada glifo é uma lista de 7 textos de 5 caracteres, onde '#' é pixel aceso e
 *   '.' é apagado. Por exemplo, a letra T:
 *
 *        #####
 *        ..#..
 *        ..#..          (7 linhas, de cima para baixo)
 *        ..#..
 *        ..#..
 *        ..#..
 *        ..#..
 *
 *   É menos compacto que guardar bits, mas qualquer pessoa confere o desenho
 *   direto no código (e o teste imprime/decodifica o resultado).
 *
 * CARACTERES SUPORTADOS: A-Z, 0-9, '.' e espaço — exatamente o que o REQ-12
 * precisa (placa, "BRL 4.00", "SIMULATED PAYMENT", "NOT PIX"). Qualquer outro
 * caractere vira um espaço em branco (nunca quebra o desenho).
 *
 * MÓDULO PURO: nenhum include do Zephyr, só dados e consultas.
 */
#ifndef PARKING_FONT5X7_H
#define PARKING_FONT5X7_H

#include <stdbool.h>
#include <stdint.h>

#define FONT_COLS 5U /* colunas de pixels de uma letra */
#define FONT_ROWS 7U /* linhas de pixels de uma letra */

/* Um glifo: 7 linhas de 5 caracteres ('#' aceso, '.' apagado). */
struct font_glyph {
	char character;
	const char *rows[FONT_ROWS];
};

/* Devolve o glifo de `c` (ou o do espaço, em branco, se `c` não existir na fonte). Nunca NULL. */
const struct font_glyph *font_lookup(char c);

/* true se o pixel (col, row) do glifo está aceso. Fora da grade 5x7 => false. */
bool font_pixel(const struct font_glyph *glyph, unsigned int col, unsigned int row);

/* true se `c` tem desenho próprio na fonte. */
bool font_has_glyph(char c);

#endif /* PARKING_FONT5X7_H */
