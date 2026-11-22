/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file hw_uart.c
 * @brief Drivers de UART do Zephyr em modo interrupt-driven, um buffer por porta.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include "hw_uart.h"

#define RX_MEM		256
#define TX_MEM		FRAME_MAX
#define RX_CHUNK	16

struct hw_uart {
	const struct device *dev;
	struct ring_buf rx;
	uint8_t rx_mem[RX_MEM];

	/* Campos de transmissao: escritos pela thread e pela interrupcao. */
	volatile bool tx_busy;
	volatile uint16_t tx_len;
	volatile uint16_t tx_pos;
	uint8_t tx_mem[TX_MEM];

	struct at_parser at;
	struct p1_parser p1;
};

static struct hw_uart ports[HW_UART_PORT_COUNT] = {
	[HW_UART_ENTRY_CAMERA] = { .dev = DEVICE_DT_GET(DT_ALIAS(entry_camera)) },
	[HW_UART_EXIT_CAMERA] = { .dev = DEVICE_DT_GET(DT_ALIAS(exit_camera)) },
	[HW_UART_PAYMENT] = { .dev = DEVICE_DT_GET(DT_ALIAS(payment_terminal)) },
};

static void hw_uart_isr(const struct device *dev, void *user_data)
{
	struct hw_uart *port = user_data;
	uint8_t chunk[RX_CHUNK];
	int n;

	uart_irq_update(dev);

	if (uart_irq_rx_ready(dev)) {
		while ((n = uart_fifo_read(dev, chunk, sizeof(chunk))) > 0) {
			/*
			 * O buffer tem 256 bytes e o maior quadro 128: so perde
			 * dado se o firmware parar de drenar por muito tempo.
			 */
			(void)ring_buf_put(&port->rx, chunk, (uint32_t)n);
		}
	}

	if (uart_irq_tx_ready(dev) && port->tx_busy) {
		n = uart_fifo_fill(dev, &port->tx_mem[port->tx_pos],
				   (int)(port->tx_len - port->tx_pos));
		if (n > 0) {
			port->tx_pos = (uint16_t)(port->tx_pos + (uint16_t)n);
		}
		if (port->tx_pos >= port->tx_len) {
			port->tx_busy = false;
			uart_irq_tx_disable(dev);
		}
	}
}

int hw_uart_init(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(ports); i++) {
		struct hw_uart *port = &ports[i];

		if (!device_is_ready(port->dev)) {
			return -ENODEV;
		}
		ring_buf_init(&port->rx, sizeof(port->rx_mem), port->rx_mem);
		at_parser_reset(&port->at);
		p1_parser_reset(&port->p1);

		if (uart_irq_callback_user_data_set(port->dev, hw_uart_isr, port) != 0) {
			return -ENODEV;
		}
		uart_irq_rx_enable(port->dev);
	}
	return 0;
}

int hw_uart_send(enum hw_uart_port port_id, const char *line)
{
	struct hw_uart *port;
	const char *end;
	size_t len;
	size_t tail;

	if ((unsigned int)port_id >= HW_UART_PORT_COUNT || line == NULL) {
		return -EINVAL;
	}
	port = &ports[port_id];

	/* O terminal termina em LF; as cameras, em CR LF. */
	end = (port_id == HW_UART_PAYMENT) ? "\n" : "\r\n";
	tail = strlen(end);
	len = strlen(line);
	if (len + tail > TX_MEM) {
		return -EMSGSIZE;
	}
	if (port->tx_busy) {
		return -EBUSY;
	}

	/* Desliga a transmissao antes de mexer nos campos que a ISR usa. */
	uart_irq_tx_disable(port->dev);
	memcpy(port->tx_mem, line, len);
	memcpy(&port->tx_mem[len], end, tail);
	port->tx_len = (uint16_t)(len + tail);
	port->tx_pos = 0;
	port->tx_busy = true;
	uart_irq_tx_enable(port->dev);

	return 0;
}

bool hw_uart_poll_plate(enum hw_uart_port port_id, struct at_plate *out)
{
	struct hw_uart *port;
	uint8_t chunk[RX_CHUNK];
	uint32_t n;
	int64_t now;

	if ((unsigned int)port_id > HW_UART_EXIT_CAMERA || out == NULL) {
		return false;
	}
	port = &ports[port_id];
	now = k_uptime_get();

	while ((n = ring_buf_get(&port->rx, chunk, sizeof(chunk))) > 0) {
		for (uint32_t i = 0; i < n; i++) {
			if (at_parser_feed(&port->at, (char)chunk[i], now, out) > 0) {
				return true;
			}
		}
	}
	return false;
}

bool hw_uart_poll_payment(struct p1_result *out)
{
	struct hw_uart *port = &ports[HW_UART_PAYMENT];
	uint8_t chunk[RX_CHUNK];
	uint32_t n;
	int64_t now;

	if (out == NULL) {
		return false;
	}
	now = k_uptime_get();

	while ((n = ring_buf_get(&port->rx, chunk, sizeof(chunk))) > 0) {
		for (uint32_t i = 0; i < n; i++) {
			if (p1_parser_feed(&port->p1, (char)chunk[i], now, out) > 0) {
				return true;
			}
		}
	}
	return false;
}
