#ifndef PARKING_UART_RX_H
#define PARKING_UART_RX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { PARKING_UART_RX_CAPACITY = 256 };

/* Callers serialize ISR writes and worker reads. Zero initialization is empty. */
struct parking_uart_rx {
	uint8_t bytes[PARKING_UART_RX_CAPACITY];
	int64_t received_ms[PARKING_UART_RX_CAPACITY];
	size_t head;
	size_t count;
	bool discard;
	bool lost;
};

/* Drop all queued bytes and resynchronize at the next LF. */
void parking_uart_rx_lost(struct parking_uart_rx *rx);

void parking_uart_rx_put(struct parking_uart_rx *rx, uint8_t byte, int64_t received_ms);

/* The marker is delivered before any byte queued after resynchronization. */
int parking_uart_rx_get(struct parking_uart_rx *rx, uint8_t *byte, int64_t *received_ms);

#endif
