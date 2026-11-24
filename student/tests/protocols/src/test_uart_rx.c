#include <errno.h>

#include <zephyr/ztest.h>

#include "camera.h"
#include "parking_uart_rx.h"

static struct parking_uart_rx receiver;

static void reset_receiver(void *fixture)
{
	ARG_UNUSED(fixture);
	receiver = (struct parking_uart_rx){0};
}

static void enqueue(const char *bytes, int64_t received_ms)
{
	for (; *bytes != '\0'; bytes++) {
		parking_uart_rx_put(&receiver, (uint8_t)*bytes, received_ms);
	}
}

static void fill_receiver(void)
{
	for (int i = 0; i < PARKING_UART_RX_CAPACITY; i++) {
		parking_uart_rx_put(&receiver, 'x', i);
	}
}

static int parse_queued(struct camera_parser *parser, struct camera_plate *result)
{
	int count = 0;
	uint8_t byte;
	int64_t received_ms;
	int rc;

	while ((rc = parking_uart_rx_get(&receiver, &byte, &received_ms)) != 0) {
		if (rc == -EOVERFLOW) {
			camera_parser_reset(parser);
		} else {
			count += camera_parser_feed(parser, byte, received_ms, result);
		}
	}
	return count;
}

ZTEST(uart_rx, test_overflow_invalidates_prefix_and_recovers)
{
	struct camera_parser parser = {0};
	struct camera_plate result = {0};

	enqueue("+PLATE:1,\"ABC", 10);
	zassert_equal(parse_queued(&parser, &result), 0);
	fill_receiver();
	parking_uart_rx_put(&receiver, 'x', 300);
	/* A valid-looking suffix must never complete the pre-loss parser. */
	enqueue("1D23\",93\r\n", 301);
	enqueue("+PLATE:2,\"ABC1D23\",93\r\n", 302);
	zassert_equal(parse_queued(&parser, &result), 1);
	zassert_equal(result.id, 2);
}

ZTEST(uart_rx, test_overflow_on_lf_preserves_next_frame)
{
	uint8_t byte;
	int64_t received_ms;

	fill_receiver();
	parking_uart_rx_put(&receiver, '\n', 300);
	enqueue("Q\n", 301);
	zassert_equal(parking_uart_rx_get(&receiver, &byte, &received_ms), -EOVERFLOW);
	zassert_equal(parking_uart_rx_get(&receiver, &byte, &received_ms), 1);
	zassert_equal(byte, 'Q');
	zassert_equal(received_ms, 301);
	zassert_equal(parking_uart_rx_get(&receiver, &byte, &received_ms), 1);
	zassert_equal(byte, '\n');
	zassert_equal(parking_uart_rx_get(&receiver, &byte, &received_ms), 0);
}

ZTEST(uart_rx, test_loss_marker_is_safe_before_lf_arrives)
{
	uint8_t byte;
	int64_t received_ms;

	parking_uart_rx_lost(&receiver);
	zassert_equal(parking_uart_rx_get(&receiver, &byte, &received_ms), -EOVERFLOW);
	enqueue("partial\r", 12);
	zassert_equal(parking_uart_rx_get(&receiver, &byte, &received_ms), 0);
	enqueue("\nQ", 13);
	zassert_equal(parking_uart_rx_get(&receiver, &byte, &received_ms), 1);
	zassert_equal(byte, 'Q');
	zassert_equal(received_ms, 13);
}

ZTEST(uart_rx, test_preserves_order_and_virtual_time_across_wrap)
{
	uint8_t byte;
	int64_t received_ms;

	fill_receiver();
	zassert_equal(parking_uart_rx_get(&receiver, &byte, &received_ms), 1);
	zassert_equal(byte, 'x');
	zassert_equal(received_ms, 0);
	parking_uart_rx_put(&receiver, 'Q', 300);
	for (int i = 1; i < PARKING_UART_RX_CAPACITY; i++) {
		zassert_equal(parking_uart_rx_get(&receiver, &byte, &received_ms), 1);
		zassert_equal(byte, 'x');
		zassert_equal(received_ms, i);
	}
	zassert_equal(parking_uart_rx_get(&receiver, &byte, &received_ms), 1);
	zassert_equal(byte, 'Q');
	zassert_equal(received_ms, 300);
	zassert_equal(parking_uart_rx_get(&receiver, &byte, &received_ms), 0);
}

ZTEST_SUITE(uart_rx, NULL, NULL, reset_receiver, NULL, NULL);
