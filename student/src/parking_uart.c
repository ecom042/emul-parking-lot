#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

#include "parking_uart.h"
#include "parking_uart_rx.h"
#include "services.h"

enum { WORKER_STACK = 1536, WORKER_PRIORITY = 2, IDLE_POLL_MS = 10 };

struct parking_uart {
	const struct device *dev;
	struct k_sem *wake;
	struct k_spinlock rx_lock;
	struct parking_uart_rx rx;
};

static struct parking_uart cameras[PARKING_LANE_COUNT] = {
	[PARKING_ENTRY] = { .dev = DEVICE_DT_GET(DT_ALIAS(entry_camera)) },
	[PARKING_EXIT] = { .dev = DEVICE_DT_GET(DT_ALIAS(exit_camera)) },
};
static struct parking_uart terminal = { .dev = DEVICE_DT_GET(DT_ALIAS(payment_terminal)) };

K_SEM_DEFINE(worker_wake, 0, 1);
K_THREAD_STACK_DEFINE(worker_stack, WORKER_STACK);
static struct k_thread worker_thread;

static void uart_mark_lost(struct parking_uart *uart)
{
	k_spinlock_key_t key = k_spin_lock(&uart->rx_lock);

	parking_uart_rx_lost(&uart->rx);
	k_spin_unlock(&uart->rx_lock, key);
}

static void uart_isr(const struct device *dev, void *user_data)
{
	struct parking_uart *uart = user_data;
	uint8_t chunk[16];

	uart_irq_update(dev);
	bool fifo_uncertain = uart_err_check(dev) > 0;

	if (fifo_uncertain) {
		uart_mark_lost(uart);
	}
	while (uart_irq_rx_ready(dev) > 0) {
		int n = uart_fifo_read(dev, chunk, sizeof(chunk));

		if (n < 0) {
			uart_mark_lost(uart);
		}
		if (n <= 0) {
			break;
		}
		/* The hardware cannot locate a loss among the bytes left in its FIFO. */
		if (fifo_uncertain) {
			continue;
		}
		int64_t received_ms = k_uptime_get();
		k_spinlock_key_t key = k_spin_lock(&uart->rx_lock);

		for (int i = 0; i < n; i++) {
			parking_uart_rx_put(&uart->rx, chunk[i], received_ms);
		}
		k_spin_unlock(&uart->rx_lock, key);
	}
	k_sem_give(uart->wake);
}

static int uart_open(struct parking_uart *uart, struct k_sem *wake)
{
	if (!device_is_ready(uart->dev)) {
		return -ENODEV;
	}
	int rc = uart_irq_callback_user_data_set(uart->dev, uart_isr, uart);

	if (rc == 0) {
		uart->wake = wake;
		uart_irq_rx_enable(uart->dev);
	}
	return rc;
}

static int uart_receive(struct parking_uart *uart, uint8_t *byte, int64_t *received_ms)
{
	k_spinlock_key_t key = k_spin_lock(&uart->rx_lock);
	int rc = parking_uart_rx_get(&uart->rx, byte, received_ms);

	k_spin_unlock(&uart->rx_lock, key);
	return rc;
}

static int uart_send(struct parking_uart *uart, const uint8_t *data, size_t len)
{
	if (uart->wake == NULL) {
		return -ENODEV;
	}
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uart->dev, data[i]);
	}
	return 0;
}

int parking_terminal_init(struct k_sem *wake)
{
	return uart_open(&terminal, wake);
}

int parking_terminal_read(uint8_t *byte, int64_t *received_ms)
{
	return uart_receive(&terminal, byte, received_ms);
}

int parking_terminal_send(const uint8_t *data, size_t len)
{
	return uart_send(&terminal, data, len);
}

static int camera_send(enum parking_lane_id lane, const uint8_t *data, size_t len)
{
	return uart_send(&cameras[lane], data, len);
}

static void wake(void)
{
	k_sem_give(&worker_wake);
}

static const struct services_io uart_io = {
	.send = camera_send,
	.post = parking_post_reply,
	.wake = wake,
};

static void worker(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (true) {
		(void)k_sem_take(&worker_wake, K_MSEC(IDLE_POLL_MS));

		for (int i = 0; i < PARKING_LANE_COUNT; i++) {
			for (int n = 0; n <= PARKING_UART_RX_CAPACITY; n++) {
				uint8_t byte;
				int64_t received_ms;
				int rc = uart_receive(&cameras[i], &byte, &received_ms);

				if (rc == 0) {
					break;
				}
				if (rc == -EOVERFLOW) {
					services_camera_resync(i);
				} else {
					services_receive(i, byte, received_ms);
				}
			}
		}
		services_process(k_uptime_get());
	}
}

int parking_services_start(void)
{
	services_init(&uart_io);
	for (int i = 0; i < PARKING_LANE_COUNT; i++) {
		int rc = uart_open(&cameras[i], &worker_wake);

		if (rc != 0) {
			return rc;
		}
	}
	k_thread_create(&worker_thread, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack),
			worker, NULL, NULL, NULL, K_PRIO_PREEMPT(WORKER_PRIORITY), 0, K_NO_WAIT);
	k_thread_name_set(&worker_thread, "protocols");
	return parking_register_services(&parking_services);
}
