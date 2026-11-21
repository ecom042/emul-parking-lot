#include <errno.h>

#include "parking_uart_rx.h"

void parking_uart_rx_lost(struct parking_uart_rx *rx)
{
	rx->head = 0;
	rx->count = 0;
	rx->discard = true;
	rx->lost = true;
}

void parking_uart_rx_put(struct parking_uart_rx *rx, uint8_t byte, int64_t received_ms)
{
	if (rx->count == PARKING_UART_RX_CAPACITY) {
		parking_uart_rx_lost(rx);
	}
	if (rx->discard) {
		rx->discard = byte != '\n';
		return;
	}
	size_t tail = (rx->head + rx->count) % PARKING_UART_RX_CAPACITY;

	rx->bytes[tail] = byte;
	rx->received_ms[tail] = received_ms;
	rx->count++;
}

int parking_uart_rx_get(struct parking_uart_rx *rx, uint8_t *byte, int64_t *received_ms)
{
	if (rx->lost) {
		rx->lost = false;
		return -EOVERFLOW;
	}
	if (rx->count == 0) {
		return 0;
	}
	*byte = rx->bytes[rx->head];
	*received_ms = rx->received_ms[rx->head];
	rx->head = (rx->head + 1) % PARKING_UART_RX_CAPACITY;
	rx->count--;
	return 1;
}
