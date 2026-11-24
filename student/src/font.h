/* Fonte bitmap 5x7 para o display. Veja font.c. */
#ifndef FONT_H_
#define FONT_H_

#include <stdint.h>

#define FONT_WIDTH 5
#define FONT_HEIGHT 7
#define FONT_GLYPH_COUNT 38

/** As 7 linhas do glifo de c (bit 4 = coluna da esquerda). */
const uint8_t *font_glyph(char c);

#endif /* FONT_H_ */
