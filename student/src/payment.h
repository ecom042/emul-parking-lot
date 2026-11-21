#ifndef PARKING_PAYMENT_H
#define PARKING_PAYMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAYMENT_FRAME_MAX 128
#define PAYMENT_UUID_SIZE 37

enum payment_event { PAYMENT_NONE, PAYMENT_PAID, PAYMENT_DENIED };

/* Owned by the exit worker. Attempts is zero when no charge may authorize. */
struct payment {
	int (*send)(const uint8_t *data, size_t len);
	uint32_t serial;
	uint32_t amount;
	unsigned int attempts;
	int64_t sent_ms;
	size_t charge_len;
	char uuid[PAYMENT_UUID_SIZE];
	char charge[PAYMENT_FRAME_MAX];

	int64_t received_ms;
	size_t rx_len;
	bool rx_discard;
	char rx[PAYMENT_FRAME_MAX];
};

void payment_init(struct payment *payment, int (*send)(const uint8_t *, size_t));

/* Starts a charge and sends its first frame. A send failure has uncertain
 * delivery, so it consumes an attempt and the same frame will be retried.
 */
int payment_begin(struct payment *payment, const char *plate, uint32_t amount, int64_t now_ms);

/* Use the byte arrival time for RX deadlines and current uptime for retries. */
enum payment_event payment_receive(struct payment *payment, uint8_t byte, int64_t now_ms);
enum payment_event payment_poll(struct payment *payment, int64_t now_ms);

/* The transport has discarded bytes through LF and will resume at a new frame. */
void payment_rx_lost(struct payment *payment);

/* Invalidate before sending CANCEL. Safe to call when there is no charge. */
void payment_cancel(struct payment *payment);

#endif
