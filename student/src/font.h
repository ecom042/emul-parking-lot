/*
 * Fonte bitmap 5x7 própria, só com os caracteres que o display usa:
 * A-Z, 0-9, ponto e espaço.
 */
#ifndef PARKING_FONT_H
#define PARKING_FONT_H

#include <stdint.h>

#define FONT_W 5
#define FONT_H 7

/**
 * Linhas do glifo de c, de cima para baixo; em cada linha o bit 4 é a
 * coluna da esquerda. Caracteres sem glifo saem em branco (espaço).
 */
const uint8_t *font_glyph(char c);

#endif /* PARKING_FONT_H */
