#include "services/payment.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/sys/__assert.h>

#include "hw/lane_hw.h"
#include "hw/uart_line.h"
#include "services/events.h"
#include "services/line_framer.h"
#include "services/p1_proto.h"

#define PAY_SENDS      3
#define PAY_SEND_MS    1000
#define PAY_READ_CHUNK 32
#define PAY_FRAME_MAX  80 /* a CHARGE is at most 67 bytes */

enum pay_state {
	PAY_IDLE,    /**< no charge running */
	PAY_SEND,    /**< started: the next poll sends the first CHARGE */
	PAY_WAITING, /**< CHARGE sent: waiting for its PAID */
};

/*
 * There is a single terminal, on the exit lane. payment_start(),
 * payment_cancel() and payment_poll() are called only by the exit lane
 * thread, so there is no mutex.
 */
static struct pay {
	struct line_framer framer;
	enum pay_state state;
	uint8_t sends;                /**< CHARGE frames sent for the current charge */
	uint32_t charges;             /**< charges created since init: source of the UUIDs */
	char uuid[UUID_LEN + 1];      /**< UUID of the current charge */
	uint32_t cents;               /**< frozen amount of the current charge */
	char frame[PAY_FRAME_MAX];    /**< the CHARGE frame, resent unchanged */
	size_t frame_len;
	int64_t sent_ms;              /**< virtual time of the last send */
} pay;

/* Send the stored CHARGE frame and start its 1000 ms deadline. */
static void send_charge(int64_t now_ms)
{
	uart_line_write(payment_terminal_get(), pay.frame, pay.frame_len);
	pay.sends++;
	pay.sent_ms = now_ms;
	pay.state = PAY_WAITING;
}

static void send_cancel(void)
{
	char cmd[64];
	int len = p1_proto_format_cancel(pay.uuid, cmd, sizeof(cmd));

	__ASSERT_NO_MSG(len > 0);
	uart_line_write(payment_terminal_get(), cmd, (size_t)len);
}

/* No PAID after all the sends: invalidate the UUID, then tell the machine.
 * The state is consistent before the event is published.
 */
static void fail(void)
{
	pay.state = PAY_IDLE;
	send_cancel();
	(void)events_post(LANE_EXIT, EV_PAYMENT_FAILED);
}

static void handle_line(void)
{
	struct p1_reply reply;

	p1_proto_parse(line_framer_line(&pay.framer), &reply);
	/* ERROR and invalid lines never restart the deadline; a reply for an
	 * old UUID, another amount or with no charge running is ignored.
	 */
	if (pay.state != PAY_WAITING ||
	    (reply.kind != P1_REPLY_PAID && reply.kind != P1_REPLY_CANCELLED) ||
	    strcmp(reply.uuid, pay.uuid) != 0 || reply.amount != pay.cents) {
		return;
	}
	pay.state = PAY_IDLE;
	if (reply.kind == P1_REPLY_PAID) {
		(void)events_post(LANE_EXIT, EV_PAID);
	} else {
		/* The terminal already cancelled it: no CANCEL to send. */
		(void)events_post(LANE_EXIT, EV_PAYMENT_FAILED);
	}
}

int payment_init(void)
{
	memset(&pay, 0, sizeof(pay));
	line_framer_init(&pay.framer, LINE_LF);
	return uart_line_init(payment_terminal_get());
}

int payment_start(const char *plate, uint32_t cents)
{
	char uuid[UUID_LEN + 1];
	char frame[PAY_FRAME_MAX];
	int len;

	if (pay.state != PAY_IDLE) {
		return -EBUSY;
	}
	if (plate == NULL) {
		return -EINVAL;
	}
	p1_uuid_format(pay.charges + 1, uuid);
	len = p1_proto_format_charge(uuid, plate, cents, frame, sizeof(frame));
	if (len < 0) {
		return -EINVAL;
	}
	pay.charges++;
	memcpy(pay.uuid, uuid, sizeof(pay.uuid));
	memcpy(pay.frame, frame, (size_t)len);
	pay.frame_len = (size_t)len;
	pay.cents = cents;
	pay.sends = 0;
	pay.state = PAY_SEND;
	return 0;
}

void payment_cancel(void)
{
	bool was_sent = (pay.state == PAY_WAITING);

	if (pay.state == PAY_IDLE) {
		return;
	}
	pay.state = PAY_IDLE;
	if (was_sent) {
		send_cancel();
	}
}

void payment_poll(int64_t now_ms)
{
	uint8_t buf[PAY_READ_CHUNK];
	int n = uart_line_read(payment_terminal_get(), buf, sizeof(buf));

	/* Bytes first: a reply that arrived in time counts even if the
	 * deadline is reached in this same poll.
	 */
	for (int i = 0; i < n; i++) {
		if (line_framer_feed(&pay.framer, buf[i], now_ms) == LINE_READY) {
			handle_line();
		}
	}
	line_framer_check(&pay.framer, now_ms);

	if (pay.state == PAY_SEND) {
		send_charge(now_ms);
	} else if (pay.state == PAY_WAITING && now_ms - pay.sent_ms >= PAY_SEND_MS) {
		if (pay.sends < PAY_SENDS) {
			send_charge(now_ms);
		} else {
			fail();
		}
	}
}
