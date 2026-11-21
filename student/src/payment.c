#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "camera.h"
#include "payment.h"

enum {
	MAX_ATTEMPTS = 3,
	MAX_AMOUNT = 1000000,
	REPLY_MS = 1000,
	FRAME_MS = 1000,
};

void payment_init(struct payment *payment, int (*send)(const uint8_t *, size_t))
{
	*payment = (struct payment){ .send = send };
}

static void send_charge(struct payment *payment, int64_t now_ms)
{
	payment->attempts++;
	payment->sent_ms = now_ms;
	(void)payment->send((const uint8_t *)payment->charge, payment->charge_len);
}

int payment_begin(struct payment *payment, const char *plate, uint32_t amount, int64_t now_ms)
{
	if (payment->attempts != 0) {
		return -EBUSY;
	}
	if (plate == NULL || !camera_plate_valid(plate, strlen(plate)) || amount == 0 ||
	    amount > MAX_AMOUNT) {
		return -EINVAL;
	}
	if (payment->serial == UINT32_MAX) {
		return -EOVERFLOW;
	}

	payment->serial++;
	payment->amount = amount;
	(void)snprintf(payment->uuid, sizeof(payment->uuid),
		       "00000000-0000-4000-8000-%012" PRIx32, payment->serial);
	(void)snprintf(payment->charge, sizeof(payment->charge), "P1 CHARGE %s %s %" PRIu32 " BRL\n",
		       payment->uuid, plate, amount);
	payment->charge_len = strlen(payment->charge);
	send_charge(payment, now_ms);
	return 0;
}

void payment_cancel(struct payment *payment)
{
	if (payment->attempts == 0) {
		return;
	}

	char frame[PAYMENT_FRAME_MAX];

	payment->attempts = 0;
	(void)snprintf(frame, sizeof(frame), "P1 CANCEL %s\n", payment->uuid);
	(void)payment->send((const uint8_t *)frame, strlen(frame));
}

enum payment_event payment_poll(struct payment *payment, int64_t now_ms)
{
	if (payment->attempts == 0 || now_ms - payment->sent_ms < REPLY_MS) {
		return PAYMENT_NONE;
	}
	if (payment->attempts == MAX_ATTEMPTS) {
		payment_cancel(payment);
		return PAYMENT_DENIED;
	}

	send_charge(payment, now_ms);
	return PAYMENT_NONE;
}

void payment_rx_lost(struct payment *payment)
{
	payment->rx_len = 0;
	payment->rx_discard = false;
}

static enum payment_event complete_result(struct payment *payment, int64_t now_ms)
{
	/* The UUID remains current between retries, even if a reply started before
	 * the most recent send. After the final attempt there is a hard deadline.
	 */
	if (payment->attempts == 0 ||
	    (payment->attempts == MAX_ATTEMPTS && now_ms - payment->sent_ms >= REPLY_MS)) {
		return PAYMENT_NONE;
	}

	/* Compare against the only two possible canonical results for this
	 * charge. Other UUIDs, values, delimiters or currency cannot match.
	 */
	char expected[PAYMENT_FRAME_MAX];

	(void)snprintf(expected, sizeof(expected), "P1 RESULT %s PAID %" PRIu32 " BRL",
		       payment->uuid, payment->amount);
	if (strcmp(payment->rx, expected) == 0) {
		payment->attempts = 0;
		return PAYMENT_PAID;
	}
	(void)snprintf(expected, sizeof(expected), "P1 RESULT %s CANCELLED %" PRIu32 " BRL",
		       payment->uuid, payment->amount);
	if (strcmp(payment->rx, expected) == 0) {
		payment_cancel(payment);
		return PAYMENT_DENIED;
	}
	return PAYMENT_NONE;
}

enum payment_event payment_receive(struct payment *payment, uint8_t byte, int64_t now_ms)
{
	if (payment->rx_len != 0 && now_ms - payment->received_ms >= FRAME_MS) {
		payment->rx_discard = true;
	}
	if (byte == '\n') {
		enum payment_event event = PAYMENT_NONE;

		if (!payment->rx_discard && payment->rx_len != 0) {
			payment->rx[payment->rx_len] = '\0';
			event = complete_result(payment, now_ms);
		}
		payment_rx_lost(payment);
		return event;
	}
	if (payment->rx_discard) {
		return PAYMENT_NONE;
	}
	if (byte < 0x20 || byte > 0x7e || payment->rx_len == PAYMENT_FRAME_MAX - 1) {
		payment->rx_discard = true;
		return PAYMENT_NONE;
	}
	if (payment->rx_len == 0) {
		payment->received_ms = now_ms;
	}
	payment->rx[payment->rx_len++] = (char)byte;
	return PAYMENT_NONE;
}
