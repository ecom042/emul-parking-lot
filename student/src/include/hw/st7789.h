#ifndef PARKING_HW_ST7789_H_
#define PARKING_HW_ST7789_H_

/*
 * ST7789V display on SPI: the controller subset the course model supports.
 * 240x320, RGB565, high byte first, 8-bit four-wire SPI. No drawing or text
 * here: services/screen decides what to show.
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

#define ST7789_WIDTH 240
#define ST7789_HEIGHT 320

struct st7789 {
	struct spi_dt_spec spi;
	struct gpio_dt_spec dc;    /**< data (active) / command (inactive) */
	struct gpio_dt_spec reset; /**< hardware reset, active low on the board */
};

/** @brief Initializer from a devicetree node of "course,st7789v-reference". */
#define ST7789_DT_GET(node)                                                               \
	{                                                                                 \
		.spi = SPI_DT_SPEC_GET(node, SPI_WORD_SET(8) | SPI_TRANSFER_MSB),         \
		.dc = GPIO_DT_SPEC_GET(node, dc_gpios),                                   \
		.reset = GPIO_DT_SPEC_GET(node, reset_gpios),                             \
	}

/**
 * @brief Reset and start the display: reset, SWRESET, SLPOUT, COLMOD 0x55,
 *        MADCTL 0x00, DISPON.
 *
 * Blocks for about 400 ms (the required waits, in virtual time): call it
 * before the lane threads start.
 *
 * @retval 0 on success.
 * @retval -ENODEV SPI or a GPIO controller is not ready.
 * @retval -errno error from the SPI or GPIO driver.
 */
int st7789_init(const struct st7789 *lcd);

/**
 * @brief Select the window for the next pixels (CASET, RASET, RAMWR).
 *
 * Limits are inclusive.
 *
 * @retval 0 on success.
 * @retval -EINVAL empty window or outside 240x320 (nothing is sent).
 * @retval -errno error from the SPI or GPIO driver.
 */
int st7789_set_window(const struct st7789 *lcd, uint16_t x0, uint16_t y0,
		      uint16_t x1, uint16_t y1);

/**
 * @brief Send pixel bytes (RGB565, high byte first) into the open window.
 *
 * @retval 0 on success.
 * @retval -errno error from the SPI or GPIO driver.
 */
int st7789_write_pixels(const struct st7789 *lcd, const uint8_t *data, size_t len);

/** @brief Fill a window with one RGB565 color. Same returns as set_window. */
int st7789_fill(const struct st7789 *lcd, uint16_t x0, uint16_t y0,
		uint16_t x1, uint16_t y1, uint16_t color);

#endif /* PARKING_HW_ST7789_H_ */
