/*
 * Tests of hw/uart_line on an emulated UART (zephyr,uart-emul, see
 * ../app.overlay). uart_emul_put_rx_data() plays the bytes the peer sends;
 * uart_emul_get_tx_data() reads back what the firmware wrote.
 */
#include <string.h>

#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/ztest.h>

#include "hw/uart_line.h"

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(test_uart));

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);
	uart_emul_flush_rx_data(uart);
	uart_emul_flush_tx_data(uart);
}

ZTEST_SUITE(hw_uart_line, NULL, NULL, reset, NULL, NULL);

ZTEST(hw_uart_line, test_init_accepts_a_ready_device)
{
	zassert_ok(uart_line_init(uart));
}

ZTEST(hw_uart_line, test_read_returns_the_bytes_that_arrived)
{
	uint8_t buf[16];

	uart_emul_put_rx_data(uart, (const uint8_t *)"OK\r\n", 4);
	zassert_equal(uart_line_read(uart, buf, sizeof(buf)), 4);
	zassert_mem_equal(buf, "OK\r\n", 4);
}

ZTEST(hw_uart_line, test_read_without_data_returns_zero)
{
	uint8_t buf[4];

	zassert_equal(uart_line_read(uart, buf, sizeof(buf)), 0);
}

ZTEST(hw_uart_line, test_read_stops_at_the_buffer_size_and_keeps_the_rest)
{
	uint8_t buf[3];

	uart_emul_put_rx_data(uart, (const uint8_t *)"ABCDE", 5);
	zassert_equal(uart_line_read(uart, buf, sizeof(buf)), 3);
	zassert_mem_equal(buf, "ABC", 3);
	zassert_equal(uart_line_read(uart, buf, sizeof(buf)), 2);
	zassert_mem_equal(buf, "DE", 2);
}

ZTEST(hw_uart_line, test_write_sends_every_byte_in_order)
{
	uint8_t out[16];

	uart_line_write(uart, "AT\r\n", 4);
	zassert_equal(uart_emul_get_tx_data(uart, out, sizeof(out)), 4);
	zassert_mem_equal(out, "AT\r\n", 4);
}
