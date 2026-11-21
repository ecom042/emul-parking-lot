/*
 * Tests of services/payment with FFF fakes in place of hw/uart_line.
 *
 * uart_line_read plays a script: the bytes queued with feed() come out at
 * most `size` per call. uart_line_write records every frame the service
 * sends. events_post() is the real one from fsm/lanes.c: the tests read the
 * events from the exit lane queue. line_framer, p1_proto and wire are real.
 *
 * The fakes are only DECLARED here: test_camera.c defines them (same binary).
 * Each suite installs its own custom_fake in its `before`.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "fsm/lanes.h"
#include "hw/lane_hw.h"
#include "hw/uart_line.h"
#include "services/events.h"
#include "services/payment.h"

DECLARE_FAKE_VALUE_FUNC(int, uart_line_init, const struct device *);
DECLARE_FAKE_VALUE_FUNC(int, uart_line_read, const struct device *, uint8_t *, size_t);
DECLARE_FAKE_VOID_FUNC(uart_line_write, const struct device *, const char *, size_t);

#define RX_MAX  512
#define TX_MAX  8
#define TX_SIZE 96

#define UUID1 "00000000-0000-4000-8000-000000000001"
#define UUID2 "00000000-0000-4000-8000-000000000002"

static uint8_t rx[RX_MAX];
static size_t rx_len;
static size_t rx_pos;
static size_t rx_chunk; /* most bytes handed out per call */
static char tx[TX_MAX][TX_SIZE]; /* every frame written, in order */
static unsigned int tx_count;
static const struct device *last_tx_dev;

static int fake_read(const struct device *dev, uint8_t *buf, size_t size)
{
	size_t n = MIN(MIN(size, rx_chunk), rx_len - rx_pos);

	ARG_UNUSED(dev);
	memcpy(buf, &rx[rx_pos], n);
	rx_pos += n;
	return (int)n;
}

static void fake_write(const struct device *dev, const char *data, size_t len)
{
	zassert_true(len < TX_SIZE);
	zassert_true(tx_count < TX_MAX);
	last_tx_dev = dev;
	memcpy(tx[tx_count], data, len);
	tx[tx_count][len] = '\0';
	tx_count++;
}

static void feed(const char *data, size_t len)
{
	zassert_true(rx_len + len <= RX_MAX);
	memcpy(&rx[rx_len], data, len);
	rx_len += len;
}

static void feed_str(const char *s)
{
	feed(s, strlen(s));
}

static void feed_result(const char *uuid, const char *verb, unsigned int cents)
{
	char line[96];

	snprintf(line, sizeof(line), "P1 RESULT %s %s %u BRL\n", uuid, verb, cents);
	feed_str(line);
}

/* Poll at the same instant until the scripted bytes are all consumed. */
static void drain(int64_t now)
{
	do {
		payment_poll(now);
	} while (rx_pos < rx_len);
}

static unsigned int writes(void)
{
	return tx_count;
}

static void expect_tx(unsigned int i, const char *frame)
{
	zassert_true(i < tx_count);
	zassert_str_equal(tx[i], frame);
}

static void expect_events(enum lane_event expected, unsigned int count)
{
	struct k_msgq *queue = lanes_get(LANE_EXIT)->queue;
	uint8_t event;

	zassert_equal(k_msgq_num_used_get(queue), count);
	for (unsigned int i = 0; i < count; i++) {
		zassert_ok(k_msgq_get(queue, &event, K_NO_WAIT));
		zassert_equal(event, expected);
	}
}

static void expect_no_event(void)
{
	zassert_equal(k_msgq_num_used_get(lanes_get(LANE_EXIT)->queue), 0);
}

static void start_and_send(int64_t now)
{
	zassert_ok(payment_start("ABC1D23", 400));
	payment_poll(now);
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(uart_line_init);
	RESET_FAKE(uart_line_read);
	RESET_FAKE(uart_line_write);
	FFF_RESET_HISTORY();
	uart_line_read_fake.custom_fake = fake_read;
	uart_line_write_fake.custom_fake = fake_write;
	rx_len = 0;
	rx_pos = 0;
	rx_chunk = SIZE_MAX;
	tx_count = 0;
	last_tx_dev = NULL;
	k_msgq_purge(lanes_get(LANE_EXIT)->queue);
	zassert_ok(payment_init());
}

ZTEST_SUITE(payment, NULL, NULL, reset, NULL, NULL);

ZTEST(payment, test_init_uses_terminal)
{
	RESET_FAKE(uart_line_init);
	zassert_ok(payment_init());
	zassert_equal(uart_line_init_fake.call_count, 1);
	zassert_equal_ptr(uart_line_init_fake.arg0_val, payment_terminal_get());
}

ZTEST(payment, test_init_reports_enodev)
{
	uart_line_init_fake.return_val = -ENODEV;
	zassert_equal(payment_init(), -ENODEV);
}

ZTEST(payment, test_start_sends_on_next_poll)
{
	zassert_ok(payment_start("ABC1D23", 400));
	zassert_equal(writes(), 0);
	payment_poll(10);
	zassert_equal(writes(), 1);
	expect_tx(0, "P1 CHARGE " UUID1 " ABC1D23 400 BRL\n");
	zassert_equal_ptr(last_tx_dev, payment_terminal_get());
}

ZTEST(payment, test_start_busy)
{
	zassert_ok(payment_start("ABC1D23", 400));
	zassert_equal(payment_start("ABC1D23", 400), -EBUSY);
	payment_poll(0);
	zassert_equal(payment_start("ABC1D23", 400), -EBUSY);
}

ZTEST(payment, test_invalid_arguments)
{
	zassert_equal(payment_start(NULL, 400), -EINVAL);
	zassert_equal(payment_start("abc1d23", 400), -EINVAL);
	zassert_equal(payment_start("ABC1D23", 0), -EINVAL);
	zassert_equal(payment_start("ABC1D23", 1000001), -EINVAL);
	payment_poll(0);
	zassert_equal(writes(), 0);
	/* Rejected starts did not consume the counter. */
	start_and_send(1);
	expect_tx(0, "P1 CHARGE " UUID1 " ABC1D23 400 BRL\n");
}

ZTEST(payment, test_amount_limits_accepted)
{
	zassert_ok(payment_start("ABC1D23", 1));
	payment_poll(0);
	expect_tx(0, "P1 CHARGE " UUID1 " ABC1D23 1 BRL\n");
	payment_cancel();
	zassert_ok(payment_start("ABC1D23", 1000000));
	payment_poll(1);
	expect_tx(2, "P1 CHARGE " UUID2 " ABC1D23 1000000 BRL\n");
}

ZTEST(payment, test_paid_accepted_then_new_start)
{
	start_and_send(0);
	feed_result(UUID1, "PAID", 400);
	drain(5);
	expect_events(EV_PAID, 1);
	zassert_equal(writes(), 1);
	zassert_ok(payment_start("XYZ9K88", 250));
	payment_poll(6);
	expect_tx(1, "P1 CHARGE " UUID2 " XYZ9K88 250 BRL\n");
}

ZTEST(payment, test_fragmented_paid_accepted)
{
	start_and_send(0);
	rx_chunk = 8;
	feed_result(UUID1, "PAID", 400);
	drain(5);
	expect_events(EV_PAID, 1);
}

ZTEST(payment, test_resend_boundary)
{
	start_and_send(100);
	payment_poll(1099);
	zassert_equal(writes(), 1);
	payment_poll(1100);
	zassert_equal(writes(), 2);
	zassert_str_equal(tx[0], tx[1]);
}

ZTEST(payment, test_three_sends_then_fail_with_cancel)
{
	start_and_send(0);
	payment_poll(1000);
	payment_poll(2000);
	zassert_equal(writes(), 3);
	expect_no_event();
	payment_poll(3000);
	zassert_equal(writes(), 4);
	expect_tx(3, "P1 CANCEL " UUID1 "\n");
	zassert_str_equal(tx[0], tx[1]);
	zassert_str_equal(tx[0], tx[2]);
	expect_events(EV_PAYMENT_FAILED, 1);
	payment_poll(9000);
	zassert_equal(writes(), 4);
	expect_no_event();
	zassert_ok(payment_start("ABC1D23", 400), "idle again");
}

ZTEST(payment, test_late_paid_accepted)
{
	start_and_send(0);
	payment_poll(1000);
	payment_poll(2000);
	feed_result(UUID1, "PAID", 400);
	drain(2500);
	expect_events(EV_PAID, 1);
	payment_poll(9000);
	zassert_equal(writes(), 3);
}

ZTEST(payment, test_paid_in_poll_of_last_deadline)
{
	start_and_send(0);
	payment_poll(1000);
	payment_poll(2000);
	feed_result(UUID1, "PAID", 400);
	payment_poll(2999); /* first 32 bytes */
	expect_no_event();
	payment_poll(3000); /* rest of the line, then the deadline */
	expect_events(EV_PAID, 1);
	zassert_equal(writes(), 3);
}

ZTEST(payment, test_other_amount_ignored)
{
	start_and_send(0);
	feed_result(UUID1, "PAID", 401);
	drain(5);
	expect_no_event();
	payment_poll(1000);
	payment_poll(2000);
	payment_poll(3000);
	expect_events(EV_PAYMENT_FAILED, 1);
}

ZTEST(payment, test_old_uuid_ignored)
{
	start_and_send(0);
	payment_poll(1000);
	payment_poll(2000);
	payment_poll(3000);
	expect_events(EV_PAYMENT_FAILED, 1);
	start_and_send(4000);
	expect_tx(4, "P1 CHARGE " UUID2 " ABC1D23 400 BRL\n");
	feed_result(UUID1, "PAID", 400);
	drain(4010);
	expect_no_event();
	feed_result(UUID2, "PAID", 400);
	drain(4020);
	expect_events(EV_PAID, 1);
}

ZTEST(payment, test_invalid_and_crlf_lines_ignored)
{
	start_and_send(0);
	feed_str("P1 RESULT " UUID1 " PAID broken\n");
	feed_result(UUID1, "PAID", 400);
	drain(5);
	expect_events(EV_PAID, 1);
	/* The CRLF variant is discarded by the framer. */
	start_and_send(6);
	feed_str("P1 RESULT " UUID2 " PAID 400 BRL\r\n");
	drain(7);
	expect_no_event();
}

ZTEST(payment, test_cancelled_fails_without_cancel)
{
	start_and_send(0);
	feed_result(UUID1, "CANCELLED", 400);
	drain(5);
	expect_events(EV_PAYMENT_FAILED, 1);
	zassert_equal(writes(), 1);
	payment_poll(5000);
	zassert_equal(writes(), 1);
}

ZTEST(payment, test_cancelled_other_amount_ignored)
{
	start_and_send(0);
	feed_result(UUID1, "CANCELLED", 399);
	drain(5);
	expect_no_event();
}

ZTEST(payment, test_error_does_not_restart_deadline)
{
	start_and_send(0);
	feed_str("P1 ERROR CONFLICT\n");
	drain(900);
	zassert_equal(writes(), 1);
	payment_poll(1000);
	zassert_equal(writes(), 2);
	expect_no_event();
}

ZTEST(payment, test_duplicate_paid_single_event)
{
	start_and_send(0);
	feed_result(UUID1, "PAID", 400);
	feed_result(UUID1, "PAID", 400);
	drain(5);
	expect_events(EV_PAID, 1);
}

ZTEST(payment, test_paid_after_failure_ignored)
{
	start_and_send(0);
	payment_poll(1000);
	payment_poll(2000);
	payment_poll(3000);
	expect_events(EV_PAYMENT_FAILED, 1);
	feed_result(UUID1, "PAID", 400);
	drain(3010);
	expect_no_event();
}

ZTEST(payment, test_cancel_while_waiting)
{
	start_and_send(0);
	payment_cancel();
	zassert_equal(writes(), 2);
	expect_tx(1, "P1 CANCEL " UUID1 "\n");
	expect_no_event();
	feed_result(UUID1, "PAID", 400);
	drain(5);
	payment_poll(5000);
	expect_no_event();
	zassert_equal(writes(), 2);
	start_and_send(6000);
	expect_tx(2, "P1 CHARGE " UUID2 " ABC1D23 400 BRL\n");
}

ZTEST(payment, test_cancel_before_send_or_idle_writes_nothing)
{
	payment_cancel();
	zassert_equal(writes(), 0);
	zassert_ok(payment_start("ABC1D23", 400));
	payment_cancel();
	payment_poll(0);
	zassert_equal(writes(), 0);
	expect_no_event();
}

ZTEST(payment, test_idle_bytes_ignored)
{
	feed_result(UUID1, "PAID", 400);
	drain(1);
	expect_no_event();
	zassert_equal(writes(), 0);
}

ZTEST(payment, test_overlong_line_dropped)
{
	char junk[130];

	start_and_send(0);
	memset(junk, 'A', sizeof(junk));
	junk[129] = '\n';
	feed(junk, sizeof(junk));
	feed_result(UUID1, "PAID", 400);
	drain(5);
	expect_events(EV_PAID, 1);
}
