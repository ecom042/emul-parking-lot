#ifndef PARKING_SERVICES_TEXT_H_
#define PARKING_SERVICES_TEXT_H_

/*
 * Text for the payment display: the amount format and a small bitmap font.
 *
 * Characters with a glyph: A-Z, 0-9, '.' and space. A character cell is 6x8
 * pixels at scale 1 (a 5x7 glyph plus one blank column and one blank row).
 * Text is drawn one pixel row at a time, so no framebuffer is needed.
 */

#include <stddef.h>
#include <stdint.h>

#define TEXT_CELL_W 6
#define TEXT_CELL_H 8

/**
 * @brief Format centavos as "BRL <reais>.<two digits>", e.g. 1230 -> "BRL 12.30".
 *
 * @return the length without the NUL, or -ENOSPC if @p out is too small.
 */
int text_format_brl(uint32_t cents, char *out, size_t size);

/** @brief Width of @p s in pixels at @p scale; -EINVAL if a character has no glyph. */
int text_width(const char *s, unsigned int scale);

/**
 * @brief One pixel row of @p s as RGB565, high byte first.
 *
 * @param row 0 .. TEXT_CELL_H * scale - 1.
 * @param out receives text_width() * 2 bytes.
 * @retval 0 on success.
 * @retval -EINVAL bad character, scale 0 or row out of range.
 * @retval -ENOSPC @p out is too small.
 */
int text_render_row(const char *s, unsigned int scale, unsigned int row,
		    uint16_t fg, uint16_t bg, uint8_t *out, size_t size);

#endif /* PARKING_SERVICES_TEXT_H_ */
