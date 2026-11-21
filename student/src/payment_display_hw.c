#include <errno.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

#include "payment_display_hw.h"

#define DISPLAY_NODE DT_ALIAS(payment_display)

static const struct spi_dt_spec spi =
	SPI_DT_SPEC_GET(DISPLAY_NODE, SPI_WORD_SET(8) | SPI_TRANSFER_MSB);
static const struct gpio_dt_spec dc = GPIO_DT_SPEC_GET(DISPLAY_NODE, dc_gpios);
static const struct gpio_dt_spec reset = GPIO_DT_SPEC_GET(DISPLAY_NODE, reset_gpios);

int payment_display_hw_prepare(void)
{
	if (!spi_is_ready_dt(&spi) || !gpio_is_ready_dt(&dc) || !gpio_is_ready_dt(&reset)) {
		return -ENODEV;
	}
	int rc = gpio_pin_configure_dt(&dc, GPIO_OUTPUT_INACTIVE);

	if (rc == 0) {
		rc = gpio_pin_configure_dt(&reset, GPIO_OUTPUT_INACTIVE);
	}
	return rc;
}

int payment_display_hw_reset(bool active)
{
	return gpio_pin_set_dt(&reset, active);
}

int payment_display_hw_write(bool data, uint8_t *bytes, size_t len)
{
	int rc = gpio_pin_set_dt(&dc, data);

	if (rc != 0) {
		return rc;
	}
	struct spi_buf buffer = { .buf = bytes, .len = len };
	const struct spi_buf_set tx = { .buffers = &buffer, .count = 1 };

	return spi_write_dt(&spi, &tx);
}
