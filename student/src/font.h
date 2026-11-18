/*
 * font.h — 8x16 ASCII bitmap font lookup
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PARKING_FONT_H_
#define PARKING_FONT_H_

#include <stdint.h>

#define FONT_WIDTH  8U
#define FONT_HEIGHT 16U

const uint8_t *font_get_char(char c);

#endif /* PARKING_FONT_H_ */
