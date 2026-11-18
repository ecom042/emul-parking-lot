/*
 * display.h — ST7789V display driver via SPI
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320

/* Initialize the ST7789V display (HW reset + init sequence). */
int display_init(void);

/* Clear the entire display to a given RGB565 color. */
int display_clear(uint16_t color);

/* Draw a string at the given pixel position. */
int display_draw_string(int x, int y, const char *str, uint16_t fg, uint16_t bg);

/* Draw the payment screen: plate, BRL amount, SIMULATED PAYMENT, NOT PIX. */
int display_payment_screen(const char *plate, uint32_t amount_centavos);

/* Set a window for RAMWR pixel data. */
int display_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

#endif /* DISPLAY_H */
