/*
 * payment_hw.c — UART do terminal de pagamento (alias `payment-terminal`, LPUART1).
 * Polling, pelo mesmo motivo da câmera: o modelo documenta bytes fragmentados,
 * não transporte por interrupção. uart_poll_in() nunca bloqueia.
 */
#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include "payment_hw.h"

static const struct device *const terminal = DEVICE_DT_GET(DT_ALIAS(payment_terminal));

int payment_hw_init(void)
{
	return device_is_ready(terminal) ? 0 : -ENODEV;
}

int payment_hw_send(const uint8_t *data, size_t len)
{
	if (data == NULL) {
		return -EINVAL;
	}
	for (size_t i = 0U; i < len; ++i) {
		uart_poll_out(terminal, data[i]);
	}
	return 0;
}

int payment_hw_read(uint8_t *buf, size_t max)
{
	size_t count = 0U;

	if (buf == NULL) {
		return -EINVAL;
	}
	while (count < max) {
		int rc = uart_poll_in(terminal, &buf[count]);

		if (rc == -1) {
			break; /* FIFO vazio */
		}
		if (rc != 0) {
			return -EIO;
		}
		count++;
	}
	return (int)count;
}
