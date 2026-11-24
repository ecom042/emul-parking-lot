/*
 * Tests of services/camera with FFF fakes in place of hw/uart_line.
 *
 * uart_line_read plays a script: the bytes queued with feed() come out at
 * most `size` per call. uart_line_write records what the camera sends.
 * events_post() is the real one from fsm/lanes.c: the tests read the events
 * from the lane's queue. line_framer, camera_proto and wire are real.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "fsm/lanes.h"
#include "hw/lane_hw.h"
#include "hw/uart_line.h"
#include "services/camera.h"
#include "services/events.h"

/* DEFINE_FFF_GLOBALS lives in test_exemplo.c: one per test binary. */
FAKE_VALUE_FUNC(int, uart_line_init, const struct device *);
FAKE_VALUE_FUNC(int, uart_line_read, const struct device *, uint8_t *, size_t);
FAKE_VOID_FUNC(uart_line_write, const struct device *, const char *, size_t);

#define RX_MAX 512

static uint8_t rx[RX_MAX];
static size_t rx_len;
static size_t rx_pos;
static size_t rx_chunk; /* most bytes handed out per call */
static char last_tx[64];
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
	size_t n = MIN(len, sizeof(last_tx) - 1);

	last_tx_dev = dev;
	memcpy(last_tx, data, n);
	last_tx[n] = '\0';
}

/* Queue bytes for the next polls. */
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

static unsigned int writes(void)
{
	return uart_line_write_fake.call_count;
}

static void expect_events(enum lane_id id, enum lane_event expected, unsigned int count)
{
	struct k_msgq *queue = lanes_get(id)->queue;
	uint8_t event;

	zassert_equal(k_msgq_num_used_get(queue), count);
	for (unsigned int i = 0; i < count; i++) {
		zassert_ok(k_msgq_get(queue, &event, K_NO_WAIT));
		zassert_equal(event, expected);
	}
}

static void expect_no_event(enum lane_id id)
{
	zassert_equal(k_msgq_num_used_get(lanes_get(id)->queue), 0);
}

static void expect_tx(const char *cmd)
{
	zassert_str_equal(last_tx, cmd);
}

static void start_and_send(enum lane_id id, int64_t now)
{
	zassert_ok(camera_start(id));
	camera_poll(id, now);
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
	last_tx[0] = '\0';
	last_tx_dev = NULL;
	k_msgq_purge(lanes_get(LANE_ENTRY)->queue);
	k_msgq_purge(lanes_get(LANE_EXIT)->queue);
	zassert_ok(camera_init(LANE_ENTRY));
	zassert_ok(camera_init(LANE_EXIT));
}

ZTEST_SUITE(camera, NULL, NULL, reset, NULL, NULL);

ZTEST(camera, test_init_uses_lane_camera)
{
	RESET_FAKE(uart_line_init);
	zassert_ok(camera_init(LANE_EXIT));
	zassert_equal(uart_line_init_fake.call_count, 1);
	zassert_equal_ptr(uart_line_init_fake.arg0_val, lane_hw_get(LANE_EXIT)->camera);
}

ZTEST(camera, test_init_reports_enodev)
{
	uart_line_init_fake.return_val = -ENODEV;
	zassert_equal(camera_init(LANE_ENTRY), -ENODEV);
}

ZTEST(camera, test_start_sends_on_next_poll)
{
	zassert_ok(camera_start(LANE_ENTRY));
	zassert_equal(writes(), 0);
	camera_poll(LANE_ENTRY, 10);
	zassert_equal(writes(), 1);
	expect_tx("AT+CAPTURE=1\r\n");
	zassert_equal_ptr(last_tx_dev, lane_hw_get(LANE_ENTRY)->camera);
}

ZTEST(camera, test_start_busy)
{
	zassert_ok(camera_start(LANE_ENTRY));
	zassert_equal(camera_start(LANE_ENTRY), -EBUSY);
	camera_poll(LANE_ENTRY, 0);
	zassert_equal(camera_start(LANE_ENTRY), -EBUSY);
}

ZTEST(camera, test_fragmented_plate_accepted)
{
	const char *reply = "OK\r\n+PLATE:1,\"ABC1D23\",93\r\n";
	char plate[PLATE_LEN + 1];

	start_and_send(LANE_ENTRY, 0);
	rx_chunk = 8;
	feed_str(reply);
	for (int t = 1; t <= 6; t++) {
		camera_poll(LANE_ENTRY, t);
	}
	expect_events(LANE_ENTRY, EV_PLATE_READ, 1);
	zassert_ok(camera_plate(LANE_ENTRY, plate));
	zassert_str_equal(plate, "ABC1D23");
}

ZTEST(camera, test_confidence_85_accepted)
{
	char plate[PLATE_LEN + 1];

	start_and_send(LANE_ENTRY, 0);
	feed_str("+PLATE:1,\"XYZ9K88\",85\r\n");
	camera_poll(LANE_ENTRY, 5);
	expect_events(LANE_ENTRY, EV_PLATE_READ, 1);
	zassert_ok(camera_plate(LANE_ENTRY, plate));
	zassert_str_equal(plate, "XYZ9K88");
	zassert_equal(writes(), 1);
}

ZTEST(camera, test_confidence_84_retries_now)
{
	start_and_send(LANE_ENTRY, 0);
	feed_str("+PLATE:1,\"ABC1D23\",84\r\n");
	camera_poll(LANE_ENTRY, 5);
	zassert_equal(writes(), 2);
	expect_tx("AT+CAPTURE=2\r\n");
	expect_no_event(LANE_ENTRY);
}

ZTEST(camera, test_retry_62_79_93)
{
	char plate[PLATE_LEN + 1];

	start_and_send(LANE_ENTRY, 0);
	feed_str("+PLATE:1,\"ABC1D23\",62\r\n");
	camera_poll(LANE_ENTRY, 1);
	expect_tx("AT+CAPTURE=2\r\n");
	feed_str("+PLATE:2,\"ABC1D23\",79\r\n");
	camera_poll(LANE_ENTRY, 2);
	expect_tx("AT+CAPTURE=3\r\n");
	feed_str("+PLATE:3,\"ABC1D23\",93\r\n");
	camera_poll(LANE_ENTRY, 3);
	zassert_equal(writes(), 3);
	expect_events(LANE_ENTRY, EV_PLATE_READ, 1);
	zassert_ok(camera_plate(LANE_ENTRY, plate));
}

ZTEST(camera, test_three_low_confidence_fail)
{
	char plate[PLATE_LEN + 1];

	start_and_send(LANE_ENTRY, 0);
	feed_str("+PLATE:1,\"ABC1D23\",10\r\n");
	camera_poll(LANE_ENTRY, 1);
	feed_str("+PLATE:2,\"ABC1D23\",20\r\n");
	camera_poll(LANE_ENTRY, 2);
	feed_str("+PLATE:3,\"ABC1D23\",30\r\n");
	camera_poll(LANE_ENTRY, 3);
	zassert_equal(writes(), 3);
	expect_events(LANE_ENTRY, EV_CAPTURE_FAILED, 1);
	zassert_equal(camera_plate(LANE_ENTRY, plate), -ENOENT);
}

ZTEST(camera, test_timeout_boundary)
{
	start_and_send(LANE_ENTRY, 100);
	camera_poll(LANE_ENTRY, 1099);
	zassert_equal(writes(), 1);
	camera_poll(LANE_ENTRY, 1100);
	zassert_equal(writes(), 2);
	expect_tx("AT+CAPTURE=2\r\n");
}

ZTEST(camera, test_three_timeouts_fail_then_idle)
{
	start_and_send(LANE_ENTRY, 0);
	camera_poll(LANE_ENTRY, 1000);
	camera_poll(LANE_ENTRY, 2000);
	expect_no_event(LANE_ENTRY);
	camera_poll(LANE_ENTRY, 3000);
	zassert_equal(writes(), 3);
	expect_events(LANE_ENTRY, EV_CAPTURE_FAILED, 1);
	camera_poll(LANE_ENTRY, 9000);
	zassert_equal(writes(), 3);
	expect_no_event(LANE_ENTRY);
	zassert_ok(camera_start(LANE_ENTRY), "idle again");
}

ZTEST(camera, test_ok_does_not_restart_deadline)
{
	start_and_send(LANE_ENTRY, 0);
	feed_str("OK\r\n");
	camera_poll(LANE_ENTRY, 900);
	zassert_equal(writes(), 1);
	camera_poll(LANE_ENTRY, 1000);
	zassert_equal(writes(), 2);
	expect_no_event(LANE_ENTRY);
}

ZTEST(camera, test_stale_id_ignored)
{
	start_and_send(LANE_ENTRY, 0);
	camera_poll(LANE_ENTRY, 1000); /* now waiting for ID 2 */
	feed_str("+PLATE:1,\"ABC1D23\",99\r\n");
	camera_poll(LANE_ENTRY, 1001);
	expect_no_event(LANE_ENTRY);
	zassert_equal(writes(), 2);
}

ZTEST(camera, test_invalid_replies_ignored)
{
	start_and_send(LANE_ENTRY, 0);
	feed_str("ERROR\r\n");
	feed_str("+PLATE:1,\"abc1d23\",93\r\n");
	feed_str("+PLATE:1,\"ABC1D23\",101\r\n");
	feed_str("+PLATE:1,\"ABC1D23\",93\n");
	camera_poll(LANE_ENTRY, 1);
	camera_poll(LANE_ENTRY, 2);
	camera_poll(LANE_ENTRY, 3);
	expect_no_event(LANE_ENTRY);
	zassert_equal(writes(), 1);
}

ZTEST(camera, test_overlong_line_dropped)
{
	char junk[130];
	char plate[PLATE_LEN + 1];

	start_and_send(LANE_ENTRY, 0);
	memset(junk, 'A', sizeof(junk));
	junk[129] = '\n';
	feed(junk, sizeof(junk));
	feed_str("+PLATE:1,\"ABC1D23\",93\r\n");
	for (int t = 1; t <= 8; t++) {
		camera_poll(LANE_ENTRY, t);
	}
	expect_events(LANE_ENTRY, EV_PLATE_READ, 1);
	zassert_ok(camera_plate(LANE_ENTRY, plate));
}

ZTEST(camera, test_abandon_ignores_late_reply)
{
	start_and_send(LANE_ENTRY, 0);
	camera_abandon(LANE_ENTRY);
	feed_str("+PLATE:1,\"ABC1D23\",93\r\n");
	camera_poll(LANE_ENTRY, 5);
	camera_poll(LANE_ENTRY, 2000);
	expect_no_event(LANE_ENTRY);
	zassert_equal(writes(), 1);

	start_and_send(LANE_ENTRY, 3000);
	expect_tx("AT+CAPTURE=2\r\n");
}

ZTEST(camera, test_abandon_before_send)
{
	zassert_ok(camera_start(LANE_ENTRY));
	camera_abandon(LANE_ENTRY);
	camera_poll(LANE_ENTRY, 0);
	zassert_equal(writes(), 0);
	expect_no_event(LANE_ENTRY);
}

ZTEST(camera, test_plate_cleared_on_start)
{
	char plate[PLATE_LEN + 1];

	start_and_send(LANE_ENTRY, 0);
	feed_str("+PLATE:1,\"ABC1D23\",93\r\n");
	camera_poll(LANE_ENTRY, 1);
	expect_events(LANE_ENTRY, EV_PLATE_READ, 1);
	zassert_ok(camera_plate(LANE_ENTRY, plate));

	zassert_ok(camera_start(LANE_ENTRY));
	zassert_equal(camera_plate(LANE_ENTRY, plate), -ENOENT);
}

ZTEST(camera, test_idle_bytes_ignored)
{
	feed_str("+PLATE:1,\"ABC1D23\",93\r\n");
	camera_poll(LANE_ENTRY, 1);
	camera_poll(LANE_ENTRY, 2);
	expect_no_event(LANE_ENTRY);
	zassert_equal(writes(), 0);
}

ZTEST(camera, test_lanes_independent)
{
	char plate[PLATE_LEN + 1];

	start_and_send(LANE_ENTRY, 0);
	zassert_ok(camera_start(LANE_EXIT));
	zassert_equal(camera_start(LANE_ENTRY), -EBUSY);
	camera_poll(LANE_EXIT, 0);
	zassert_equal_ptr(last_tx_dev, lane_hw_get(LANE_EXIT)->camera);
	camera_abandon(LANE_EXIT);
	zassert_ok(camera_start(LANE_EXIT));
	zassert_equal(camera_plate(LANE_ENTRY, plate), -ENOENT);
	expect_no_event(LANE_EXIT);
}
