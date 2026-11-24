#ifndef PARKING_UART_H
#define PARKING_UART_H

#include <stddef.h>
#include <stdint.h>

struct k_sem;

/* Bind the LPUART1 receiver to the terminal worker's wake semaphore. */
int parking_terminal_init(struct k_sem *wake);

/* Terminal worker only. Returns 1 for a byte, 0 when empty, or -EOVERFLOW
 * before any bytes after a loss. On loss, reset the parser: the receiver
 * has discarded the incomplete frame through LF.
 */
int parking_terminal_read(uint8_t *byte, int64_t *received_ms);
int parking_terminal_send(const uint8_t *data, size_t len);

#endif
