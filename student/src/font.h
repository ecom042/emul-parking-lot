/*
 * font.h — Simple 8x16 bitmap font
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FONT_H
#define FONT_H

#include <stdint.h>

#define FONT_WIDTH   8
#define FONT_HEIGHT  16

/* Get the bitmap data for a character (16 bytes, one per row, MSB left). */
const uint8_t *font_get_char(char c);

#endif /* FONT_H */
