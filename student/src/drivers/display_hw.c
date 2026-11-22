/*
 * display_hw.c — SPI e GPIOs reais do display.
 *
 * Tudo vem do devicetree, pelo ALIAS `payment-display` (nunca por número de pino):
 *   - a SPI do nó (SPI1) e o chip select, que o driver SPI controla sozinho;
 *   - dc-gpios   (PD15, ativo alto: 1 = dado, 0 = comando);
 *   - reset-gpios (PE0, ativo BAIXO: o Zephyr já inverte, então "ativar" = nível baixo).
 */
#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

#include "display_hw.h"

#define DISPLAY_NODE DT_ALIAS(payment_display)

/* 8 bits por palavra, bit mais significativo primeiro. */
static const struct spi_dt_spec panel_spi =
	SPI_DT_SPEC_GET(DISPLAY_NODE, SPI_WORD_SET(8) | SPI_TRANSFER_MSB);
static const struct gpio_dt_spec dc_pin = GPIO_DT_SPEC_GET(DISPLAY_NODE, dc_gpios);
static const struct gpio_dt_spec reset_pin = GPIO_DT_SPEC_GET(DISPLAY_NODE, reset_gpios);

/* Uma transação SPI com `length` bytes. */
static int spi_send(const uint8_t *data, size_t length)
{
	struct spi_buf buffer = {
		/* spi_buf quer `void *`; o driver só LÊ estes bytes. O desvio por uintptr_t
		 * evita o aviso de "cast que remove const" sem esconder nada: é só leitura. */
		.buf = (void *)(uintptr_t)data,
		.len = length,
	};
	struct spi_buf_set set = { .buffers = &buffer, .count = 1U };

	return spi_write_dt(&panel_spi, &set);
}

int display_hw_init(void)
{
	if (!spi_is_ready_dt(&panel_spi) || !gpio_is_ready_dt(&dc_pin) ||
	    !gpio_is_ready_dt(&reset_pin)) {
		return -ENODEV;
	}
	/* GPIO_OUTPUT_INACTIVE: D/C em "comando" (baixo) e RESET solto (nível alto). */
	if (gpio_pin_configure_dt(&dc_pin, GPIO_OUTPUT_INACTIVE) < 0 ||
	    gpio_pin_configure_dt(&reset_pin, GPIO_OUTPUT_INACTIVE) < 0) {
		return -EIO;
	}
	return 0;
}

int display_hw_reset(bool asserted)
{
	return gpio_pin_set_dt(&reset_pin, asserted ? 1 : 0) < 0 ? -EIO : 0;
}

int display_hw_command(uint8_t command, const uint8_t *params, size_t length)
{
	if (gpio_pin_set_dt(&dc_pin, 0) < 0 || spi_send(&command, 1U) < 0) {
		return -EIO;
	}
	if (length > 0U) {
		if (params == NULL) {
			return -EINVAL;
		}
		if (gpio_pin_set_dt(&dc_pin, 1) < 0 || spi_send(params, length) < 0) {
			return -EIO;
		}
	}
	return 0;
}

int display_hw_data(const uint8_t *data, size_t length)
{
	if (data == NULL) {
		return -EINVAL;
	}
	if (gpio_pin_set_dt(&dc_pin, 1) < 0 || spi_send(data, length) < 0) {
		return -EIO;
	}
	return 0;
}
