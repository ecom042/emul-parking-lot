#include <errno.h>
#include <string.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "services.h"

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(bool, device_ready, const struct device *);
FAKE_VALUE_FUNC(int, set_rx_handler, const struct device *, uart_irq_callback_user_data_t, void *);
FAKE_VOID_FUNC(enable_rx, const struct device *);
FAKE_VOID_FUNC(update_irq, const struct device *);
FAKE_VALUE_FUNC(int, rx_errors, const struct device *);
FAKE_VALUE_FUNC(int, rx_ready, const struct device *);
FAKE_VALUE_FUNC(int, read_fifo, const struct device *, uint8_t *, int);
FAKE_VOID_FUNC(write_byte, const struct device *, unsigned char);
FAKE_VALUE_FUNC(int64_t, uptime_ms);
FAKE_VALUE_FUNC(k_tid_t, create_worker, struct k_thread *, k_thread_stack_t *, size_t,
		k_thread_entry_t, void *, void *, void *, int, uint32_t, k_timeout_t);
FAKE_VALUE_FUNC(int, name_worker, k_tid_t, const char *);
FAKE_VALUE_FUNC(int, parking_post_reply, const struct parking_reply *);
FAKE_VALUE_FUNC(int, parking_register_services, const struct parking_services *);
FAKE_VOID_FUNC(services_init, const struct services_io *);
FAKE_VOID_FUNC(services_camera_resync, enum parking_lane_id);
FAKE_VOID_FUNC(services_receive, enum parking_lane_id, uint8_t, int64_t);
FAKE_VOID_FUNC(services_process, int64_t);

const struct parking_services parking_services = {0};

/* Compile the actual adapter against FFF instead of the Zephyr UART calls.
 * Invoke the registered handler synchronously with scripted FIFO contents.
 */
#define device_is_ready device_ready
#define uart_irq_callback_user_data_set set_rx_handler
#define uart_irq_rx_enable enable_rx
#define uart_irq_update update_irq
#define uart_err_check rx_errors
#define uart_irq_rx_ready rx_ready
#define uart_fifo_read read_fifo
#define uart_poll_out write_byte
#define k_uptime_get uptime_ms
#define k_thread_create create_worker
#define k_thread_name_set name_worker
#include "../../../src/parking_uart.c"

K_SEM_DEFINE(terminal_wake, 0, 1);

static const char *fifo_bytes;

static void reset_uart(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(device_ready);
	RESET_FAKE(set_rx_handler);
	RESET_FAKE(enable_rx);
	RESET_FAKE(update_irq);
	RESET_FAKE(rx_errors);
	RESET_FAKE(rx_ready);
	RESET_FAKE(read_fifo);
	RESET_FAKE(write_byte);
	RESET_FAKE(uptime_ms);
	RESET_FAKE(create_worker);
	RESET_FAKE(name_worker);
	RESET_FAKE(parking_register_services);
	RESET_FAKE(services_init);
	FFF_RESET_HISTORY();

	device_ready_fake.return_val = true;
	create_worker_fake.return_val = &worker_thread;
	for (int i = 0; i < PARKING_LANE_COUNT; i++) {
		cameras[i].wake = NULL;
		memset(&cameras[i].rx, 0, sizeof(cameras[i].rx));
	}
	terminal.wake = NULL;
	memset(&terminal.rx, 0, sizeof(terminal.rx));
	k_sem_reset(&worker_wake);
	k_sem_reset(&terminal_wake);
}

static int copy_fifo(const struct device *dev, uint8_t *out, int size)
{
	ARG_UNUSED(dev);
	size_t len = MIN(strlen(fifo_bytes), (size_t)size);

	memcpy(out, fifo_bytes, len);
	fifo_bytes += len;
	return (int)len;
}

static int queue_has_bytes(const struct device *dev)
{
	ARG_UNUSED(dev);
	return *fifo_bytes != '\0';
}

static void fire_rx(unsigned int index, const char *content, int64_t now_ms)
{
	fifo_bytes = content;
	rx_ready_fake.custom_fake = queue_has_bytes;
	read_fifo_fake.custom_fake = copy_fifo;
	uptime_ms_fake.return_val = now_ms;
	set_rx_handler_fake.arg1_history[index](set_rx_handler_fake.arg0_history[index],
					       set_rx_handler_fake.arg2_history[index]);
}

static void assert_terminal_byte(uint8_t expected, int64_t expected_ms)
{
	uint8_t byte;
	int64_t received_ms;

	zassert_equal(parking_terminal_read(&byte, &received_ms), 1);
	zassert_equal(byte, expected);
	zassert_equal(received_ms, expected_ms);
}

ZTEST(uart_hw, test_closed_or_unavailable_terminal_does_not_transmit)
{
	static const uint8_t data[] = "P1";

	device_ready_fake.return_val = false;
	zassert_equal(parking_terminal_init(&terminal_wake), -ENODEV);
	zassert_equal(set_rx_handler_fake.call_count, 0);
	zassert_equal(enable_rx_fake.call_count, 0);
	zassert_equal(parking_terminal_send(data, sizeof(data) - 1), -ENODEV);
	zassert_equal(write_byte_fake.call_count, 0);
}

ZTEST(uart_hw, test_callback_registration_error_leaves_terminal_closed)
{
	static const uint8_t data[] = "P1";

	set_rx_handler_fake.return_val = -EIO;
	zassert_equal(parking_terminal_init(&terminal_wake), -EIO);
	zassert_equal(enable_rx_fake.call_count, 0);
	zassert_equal(parking_terminal_send(data, sizeof(data) - 1), -ENODEV);
	zassert_equal(write_byte_fake.call_count, 0);
}

ZTEST(uart_hw, test_cameras_and_terminal_use_distinct_peripherals)
{
	static const uint8_t data[] = "A1";

	zassert_ok(parking_services_start());
	zassert_true(services_init_fake.arg0_val->post == parking_post_reply);
	zassert_equal_ptr(parking_register_services_fake.arg0_val, &parking_services);
	zassert_equal(create_worker_fake.call_count, 1);
	zassert_ok(parking_terminal_init(&terminal_wake));
	zassert_equal(enable_rx_fake.call_count, 3);
	zassert_equal_ptr(enable_rx_fake.arg0_history[0], DEVICE_DT_GET(DT_ALIAS(entry_camera)));
	zassert_equal_ptr(enable_rx_fake.arg0_history[1], DEVICE_DT_GET(DT_ALIAS(exit_camera)));
	zassert_equal_ptr(enable_rx_fake.arg0_history[2], DEVICE_DT_GET(DT_ALIAS(payment_terminal)));

	zassert_ok(services_init_fake.arg0_val->send(PARKING_ENTRY, data, sizeof(data) - 1));
	zassert_ok(services_init_fake.arg0_val->send(PARKING_EXIT, data, sizeof(data) - 1));
	zassert_ok(parking_terminal_send(data, sizeof(data) - 1));
	zassert_equal(write_byte_fake.call_count, 6);
	for (int i = 0; i < 6; i++) {
		zassert_equal_ptr(write_byte_fake.arg0_history[i], enable_rx_fake.arg0_history[i / 2]);
		zassert_equal(write_byte_fake.arg1_history[i], data[i % 2]);
	}
	services_init_fake.arg0_val->wake();
	zassert_ok(k_sem_take(&worker_wake, K_NO_WAIT));
	zassert_equal(k_sem_take(&terminal_wake, K_NO_WAIT), -EBUSY);
}

ZTEST(uart_hw, test_camera_start_failure_stops_before_registering_worker)
{
	int failures[] = { 0, -EIO };

	SET_RETURN_SEQ(set_rx_handler, failures, ARRAY_SIZE(failures));
	zassert_equal(parking_services_start(), -EIO);
	zassert_equal(enable_rx_fake.call_count, 1);
	zassert_equal(create_worker_fake.call_count, 0);
	zassert_equal(parking_register_services_fake.call_count, 0);
}

ZTEST(uart_hw, test_received_terminal_bytes_carry_virtual_time_and_independent_wake)
{
	uint8_t byte;
	int64_t received_ms;

	zassert_ok(parking_terminal_init(&terminal_wake));
	fire_rx(0, "A", 21);
	fire_rx(0, "BC", 31);
	zassert_equal(update_irq_fake.call_count, 2);
	zassert_ok(k_sem_take(&terminal_wake, K_NO_WAIT));
	zassert_equal(k_sem_take(&worker_wake, K_NO_WAIT), -EBUSY);
	assert_terminal_byte('A', 21);
	assert_terminal_byte('B', 31);
	assert_terminal_byte('C', 31);
	zassert_equal(parking_terminal_read(&byte, &received_ms), 0);
}

ZTEST(uart_hw, test_hardware_error_discards_uncertain_fifo_and_recovers)
{
	uint8_t byte;
	int64_t received_ms;

	zassert_ok(parking_terminal_init(&terminal_wake));
	fire_rx(0, "prefix", 21);
	rx_errors_fake.return_val = UART_ERROR_OVERRUN;
	fire_rx(0, "\nuncertain", 31);
	zassert_equal(parking_terminal_read(&byte, &received_ms), -EOVERFLOW);
	zassert_equal(parking_terminal_read(&byte, &received_ms), 0);
	rx_errors_fake.return_val = 0;
	fire_rx(0, "suffix\nQ\n", 41);
	assert_terminal_byte('Q', 41);
	assert_terminal_byte('\n', 41);
	zassert_equal(parking_terminal_read(&byte, &received_ms), 0);
}

ZTEST(uart_hw, test_fifo_read_error_marks_loss_but_zero_read_does_not)
{
	uint8_t byte;
	int64_t received_ms;

	zassert_ok(parking_terminal_init(&terminal_wake));
	fire_rx(0, "prefix", 21);
	rx_ready_fake.custom_fake = NULL;
	rx_ready_fake.return_val = 1;
	read_fifo_fake.custom_fake = NULL;
	read_fifo_fake.return_val = 0;
	set_rx_handler_fake.arg1_val(set_rx_handler_fake.arg0_val, set_rx_handler_fake.arg2_val);
	assert_terminal_byte('p', 21);
	read_fifo_fake.return_val = -EIO;
	set_rx_handler_fake.arg1_val(set_rx_handler_fake.arg0_val, set_rx_handler_fake.arg2_val);
	zassert_equal(parking_terminal_read(&byte, &received_ms), -EOVERFLOW);
	zassert_equal(parking_terminal_read(&byte, &received_ms), 0);
	fire_rx(0, "suffix\nR\n", 41);
	assert_terminal_byte('R', 41);
	assert_terminal_byte('\n', 41);
	zassert_equal(parking_terminal_read(&byte, &received_ms), 0);
}

ZTEST_SUITE(uart_hw, NULL, NULL, reset_uart, NULL, NULL);
