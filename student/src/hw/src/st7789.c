#include "hw/st7789.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>

#define CMD_SWRESET 0x01
#define CMD_SLPOUT 0x11
#define CMD_DISPON 0x29
#define CMD_CASET 0x2A
#define CMD_RASET 0x2B
#define CMD_RAMWR 0x2C
#define CMD_MADCTL 0x36
#define CMD_COLMOD 0x3A

#define COLMOD_RGB565 0x55
#define MADCTL_NATIVE 0x00

#define RESET_PULSE_MS 10
#define RESET_RELEASE_MS 120
#define SWRESET_MS 150
#define SLPOUT_MS 120

#define SEND_CHUNK_BYTES 128
#define FILL_CHUNK_PIXELS 64

/*
 * Send bytes with D/C set for data (true) or command (false). The SPI API takes
 * a non-const buffer, so the bytes go through a small stack copy, in chunks.
 */
static int send(const struct st7789 *lcd, bool data, const uint8_t *bytes, size_t len)
{
	uint8_t chunk[SEND_CHUNK_BYTES];
	int rc = gpio_pin_set_dt(&lcd->dc, data);

	while (rc == 0 && len > 0) {
		size_t n = MIN(len, sizeof(chunk));
		struct spi_buf buf = {.buf = chunk, .len = n};
		struct spi_buf_set set = {.buffers = &buf, .count = 1};

		memcpy(chunk, bytes, n);
		rc = spi_write_dt(&lcd->spi, &set);
		bytes += n;
		len -= n;
	}
	return rc;
}

static int command(const struct st7789 *lcd, uint8_t cmd, const uint8_t *args, size_t n)
{
	int rc = send(lcd, false, &cmd, 1);

	return (rc != 0 || n == 0) ? rc : send(lcd, true, args, n);
}

static int configure_pins(const struct st7789 *lcd)
{
	if (!spi_is_ready_dt(&lcd->spi) || !gpio_is_ready_dt(&lcd->dc) ||
	    !gpio_is_ready_dt(&lcd->reset)) {
		return -ENODEV;
	}
	int rc = gpio_pin_configure_dt(&lcd->dc, GPIO_OUTPUT_INACTIVE);

	return rc != 0 ? rc : gpio_pin_configure_dt(&lcd->reset, GPIO_OUTPUT_INACTIVE);
}

static int hardware_reset(const struct st7789 *lcd)
{
	int rc = gpio_pin_set_dt(&lcd->reset, 1);

	if (rc != 0) {
		return rc;
	}
	k_msleep(RESET_PULSE_MS);
	rc = gpio_pin_set_dt(&lcd->reset, 0);
	k_msleep(RESET_RELEASE_MS);
	return rc;
}

int st7789_init(const struct st7789 *lcd)
{
	static const uint8_t colmod = COLMOD_RGB565;
	static const uint8_t madctl = MADCTL_NATIVE;
	int rc = configure_pins(lcd);

	if (rc == 0) {
		rc = hardware_reset(lcd);
	}
	if (rc == 0) {
		rc = command(lcd, CMD_SWRESET, NULL, 0);
		k_msleep(SWRESET_MS);
	}
	if (rc == 0) {
		rc = command(lcd, CMD_SLPOUT, NULL, 0);
		k_msleep(SLPOUT_MS);
	}
	if (rc == 0) {
		rc = command(lcd, CMD_COLMOD, &colmod, 1);
	}
	if (rc == 0) {
		rc = command(lcd, CMD_MADCTL, &madctl, 1);
	}
	return rc != 0 ? rc : command(lcd, CMD_DISPON, NULL, 0);
}

/* CASET/RASET take two big-endian 16-bit limits. */
static int set_range(const struct st7789 *lcd, uint8_t cmd, uint16_t lo, uint16_t hi)
{
	const uint8_t args[] = {lo >> 8, lo & 0xFF, hi >> 8, hi & 0xFF};

	return command(lcd, cmd, args, sizeof(args));
}

int st7789_set_window(const struct st7789 *lcd, uint16_t x0, uint16_t y0,
		      uint16_t x1, uint16_t y1)
{
	if (x0 > x1 || y0 > y1 || x1 >= ST7789_WIDTH || y1 >= ST7789_HEIGHT) {
		return -EINVAL;
	}
	int rc = set_range(lcd, CMD_CASET, x0, x1);

	if (rc == 0) {
		rc = set_range(lcd, CMD_RASET, y0, y1);
	}
	return rc != 0 ? rc : command(lcd, CMD_RAMWR, NULL, 0);
}

int st7789_write_pixels(const struct st7789 *lcd, const uint8_t *data, size_t len)
{
	return send(lcd, true, data, len);
}

int st7789_fill(const struct st7789 *lcd, uint16_t x0, uint16_t y0,
		uint16_t x1, uint16_t y1, uint16_t color)
{
	uint8_t chunk[FILL_CHUNK_PIXELS * 2];
	int rc = st7789_set_window(lcd, x0, y0, x1, y1);
	size_t remaining = (size_t)(x1 - x0 + 1) * (y1 - y0 + 1);

	for (size_t i = 0; i < FILL_CHUNK_PIXELS; i++) {
		chunk[2 * i] = color >> 8;
		chunk[2 * i + 1] = color & 0xFF;
	}
	while (rc == 0 && remaining > 0) {
		size_t n = MIN(remaining, FILL_CHUNK_PIXELS);

		rc = st7789_write_pixels(lcd, chunk, n * 2);
		remaining -= n;
	}
	return rc;
}
