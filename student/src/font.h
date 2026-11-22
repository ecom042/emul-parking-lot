/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file font.h
 * @brief Fonte bitmap 5x7: maiusculas, digitos, ponto e hifen.
 */

#ifndef STUDENT_FONT_H_
#define STUDENT_FONT_H_

#include <stdint.h>

#define FONT_COLS	5
#define FONT_ROWS	7
#define FONT_ADVANCE	6

/**
 * @brief Colunas de um caractere.
 * @return 5 bytes, bit 0 = linha de cima, ou NULL para caractere ausente.
 */
const uint8_t *font_glyph(char c);

#endif /* STUDENT_FONT_H_ */
