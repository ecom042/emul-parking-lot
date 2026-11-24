#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "payment.h"

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, fake_send, const uint8_t *, size_t);

static struct payment payment;
static char sent[5][PAYMENT_FRAME_MAX];
static unsigned int sent_count;
static int send_error;

static int remember_send(const uint8_t *data, size_t len)
{
	zassert_true(sent_count < ARRAY_SIZE(sent));
	zassert_true(len < sizeof(sent[0]));
	memcpy(sent[sent_count], data, len);
	sent[sent_count++][len] = '\0';
	return send_error;
}

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(fake_send);
	FFF_RESET_HISTORY();
	fake_send_fake.custom_fake = remember_send;
	sent_count = 0;
	send_error = 0;
	payment_init(&payment, fake_send);
}

static void result(char *frame, unsigned int charge_index, const char *fields)
{
	const char *uuid = sent[charge_index] + strlen("P1 CHARGE ");

	(void)snprintf(frame, PAYMENT_FRAME_MAX, "P1 RESULT %.36s %s\n", uuid, fields);
}

static enum payment_event feed(const char *frame, int64_t now_ms)
{
	for (const char *byte = frame; *byte != '\0'; byte++) {
		enum payment_event event = payment_receive(&payment, (uint8_t)*byte, now_ms);

		if (event != PAYMENT_NONE) {
			zassert_equal(byte[1], '\0', "event before end of frame");
			return event;
		}
	}
	return PAYMENT_NONE;
}

ZTEST(payment, test_charge_and_fragmented_confirmation)
{
	char frame[PAYMENT_FRAME_MAX];

	zassert_ok(payment_begin(&payment, "ABC1D23", 300, 10));
	zassert_str_equal(sent[0], "P1 CHARGE 00000000-0000-4000-8000-000000000001 ABC1D23 300 BRL\n");
	zassert_equal(payment_begin(&payment, "XYZ9A87", 400, 11), -EBUSY);
	result(frame, 0, "PAID 300 BRL");

	for (size_t i = 0; i < strlen(frame) - 1; i++) {
		zassert_equal(payment_receive(&payment, frame[i], 20 + (int64_t)i), PAYMENT_NONE);
	}
	zassert_equal(payment_receive(&payment, '\n', 999), PAYMENT_PAID);
	zassert_equal(feed(frame, 1000), PAYMENT_NONE, "duplicate authorization");
	zassert_equal(payment_poll(&payment, 1010), PAYMENT_NONE);
	payment_cancel(&payment);
	zassert_equal(sent_count, 1);
}

ZTEST(payment, test_three_identical_sends_then_cancel)
{
	char frame[PAYMENT_FRAME_MAX];

	zassert_ok(payment_begin(&payment, "ABC1D23", 400, 0));
	zassert_equal(payment_poll(&payment, 999), PAYMENT_NONE);
	zassert_equal(sent_count, 1);
	zassert_equal(payment_poll(&payment, 1000), PAYMENT_NONE);
	zassert_equal(sent_count, 2);
	zassert_equal(payment_poll(&payment, 2000), PAYMENT_NONE);
	zassert_equal(sent_count, 3);
	zassert_str_equal(sent[0], sent[1]);
	zassert_str_equal(sent[0], sent[2]);
	zassert_equal(payment_poll(&payment, 2999), PAYMENT_NONE);

	result(frame, 0, "PAID 400 BRL");
	zassert_equal(feed(frame, 3000), PAYMENT_NONE, "result at deadline");
	zassert_equal(payment_poll(&payment, 3000), PAYMENT_DENIED);
	zassert_equal(sent_count, 4);
	zassert_str_equal(sent[3], "P1 CANCEL 00000000-0000-4000-8000-000000000001\n");
	zassert_equal(feed(frame, 3001), PAYMENT_NONE);
	zassert_equal(payment_poll(&payment, 4000), PAYMENT_NONE);
	zassert_equal(sent_count, 4);
}

ZTEST(payment, test_fragment_started_before_retry_remains_valid)
{
	char frame[PAYMENT_FRAME_MAX];

	zassert_ok(payment_begin(&payment, "ABC1234", 1000000, 0));
	result(frame, 0, "PAID 1000000 BRL");
	zassert_equal(payment_receive(&payment, frame[0], 950), PAYMENT_NONE);
	zassert_equal(payment_poll(&payment, 1000), PAYMENT_NONE);
	zassert_equal(feed(frame + 1, 1300), PAYMENT_PAID);
	zassert_equal(sent_count, 2);
	zassert_str_equal(sent[0], sent[1]);
}

ZTEST(payment, test_transport_error_reuses_charge)
{
	char frame[PAYMENT_FRAME_MAX];

	send_error = -EIO;
	zassert_ok(payment_begin(&payment, "ABC1D23", 200, 0));
	send_error = 0;
	zassert_equal(payment_poll(&payment, 1000), PAYMENT_NONE);
	zassert_equal(sent_count, 2);
	zassert_str_equal(sent[0], sent[1]);

	result(frame, 0, "PAID 200 BRL");
	zassert_equal(feed(frame, 1010), PAYMENT_PAID);
	zassert_equal(sent_count, 2);
}

ZTEST(payment, test_payment_cancellation_invalidates_current_uuid)
{
	char frame[PAYMENT_FRAME_MAX];

	zassert_ok(payment_begin(&payment, "ABC1D23", 200, 0));
	payment_cancel(&payment);
	payment_cancel(&payment);
	zassert_equal(sent_count, 2);
	zassert_str_equal(sent[1], "P1 CANCEL 00000000-0000-4000-8000-000000000001\n");
	zassert_ok(payment_begin(&payment, "ABC1D23", 200, 10));
	zassert_not_equal(strcmp(sent[0], sent[2]), 0);

	result(frame, 0, "PAID 200 BRL");
	zassert_equal(feed(frame, 20), PAYMENT_NONE, "stale UUID");
	result(frame, 2, "CANCELLED 200 BRL");
	zassert_equal(feed(frame, 30), PAYMENT_DENIED);
	zassert_equal(sent_count, 4);
	zassert_str_equal(sent[3], "P1 CANCEL 00000000-0000-4000-8000-000000000002\n");
	result(frame, 2, "PAID 200 BRL");
	zassert_equal(feed(frame, 40), PAYMENT_NONE);
}

ZTEST(payment, test_noncanonical_and_unrelated_results)
{
	static const char *const fields[] = {
		"PAID broken BRL",
		"PAID 0200 BRL",
		"PAID 201 BRL",
		"PAID 200 USD",
		"PAID  200 BRL",
		"PAID 200 BRL ",
		"PAID 200 BRL\r",
		"PAID 200 BRL\t",
		"CANCELLED 201 BRL",
	};
	char frame[PAYMENT_FRAME_MAX];

	zassert_ok(payment_begin(&payment, "ABC1D23", 200, 0));
	zassert_equal(feed("\nP1 ERROR FRAME\n", 1), PAYMENT_NONE);
	for (size_t i = 0; i < ARRAY_SIZE(fields); i++) {
		result(frame, 0, fields[i]);
		zassert_equal(feed(frame, 2), PAYMENT_NONE, "accepted %s", fields[i]);
	}
	zassert_equal(sent_count, 1);
	result(frame, 0, "PAID 200 BRL");
	zassert_equal(feed(frame, 3), PAYMENT_PAID);
}

ZTEST(payment, test_timeout_and_rx_loss_resynchronization)
{
	char frame[PAYMENT_FRAME_MAX];
	char oversized[PAYMENT_FRAME_MAX + 3];

	zassert_ok(payment_begin(&payment, "ABC1D23", 200, 0));
	result(frame, 0, "PAID 200 BRL");
	zassert_equal(payment_receive(&payment, frame[0], 10), PAYMENT_NONE);
	zassert_equal(payment_poll(&payment, 1000), PAYMENT_NONE);
	zassert_equal(feed(frame + 1, 1010), PAYMENT_NONE, "expired frame accepted");

	memset(oversized, 'x', sizeof(oversized) - 2);
	oversized[sizeof(oversized) - 2] = '\n';
	oversized[sizeof(oversized) - 1] = '\0';
	zassert_equal(feed(oversized, 1011), PAYMENT_NONE);
	zassert_equal(feed("P1 partial", 1012), PAYMENT_NONE);
	payment_rx_lost(&payment);
	zassert_equal(feed(frame, 1013), PAYMENT_PAID);
}

ZTEST(payment, test_reset_clears_pending_charge_and_partial_frame)
{
	char frame[PAYMENT_FRAME_MAX];

	zassert_ok(payment_begin(&payment, "ABC1D23", 200, 0));
	result(frame, 0, "PAID 200 BRL");
	zassert_equal(payment_receive(&payment, frame[0], 1), PAYMENT_NONE);
	payment_init(&payment, fake_send);
	zassert_equal(feed(frame, 2), PAYMENT_NONE);
	zassert_equal(payment_poll(&payment, 1000), PAYMENT_NONE);
	zassert_equal(sent_count, 1);
	zassert_ok(payment_begin(&payment, "ABC1D23", 200, 1001));
	zassert_str_equal(sent[0], sent[1], "the equipment resets its namespace too");
}

ZTEST(payment, test_invalid_requests_do_not_send)
{
	zassert_equal(payment_begin(&payment, NULL, 200, 0), -EINVAL);
	zassert_equal(payment_begin(&payment, "bad", 200, 0), -EINVAL);
	zassert_equal(payment_begin(&payment, "ABC1D23", 0, 0), -EINVAL);
	zassert_equal(payment_begin(&payment, "ABC1D23", 1000001, 0), -EINVAL);
	payment.serial = UINT32_MAX;
	zassert_equal(payment_begin(&payment, "ABC1D23", 200, 0), -EOVERFLOW);
	zassert_equal(sent_count, 0);
}

ZTEST_SUITE(payment, NULL, NULL, before_each, NULL, NULL);
