#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include "services.h"

enum { RX_RING_SIZE = 256, WORKER_STACK = 1536, WORKER_PRIORITY = 2, IDLE_POLL_MS = 10 };

struct camera_uart {
	const struct device *dev;
	struct ring_buf ring;
	uint8_t storage[RX_RING_SIZE];
};

static struct camera_uart uarts[PARKING_LANE_COUNT] = {
	[PARKING_ENTRY] = { .dev = DEVICE_DT_GET(DT_ALIAS(entry_camera)) },
	[PARKING_EXIT] = { .dev = DEVICE_DT_GET(DT_ALIAS(exit_camera)) },
};

K_SEM_DEFINE(worker_wake, 0, 1);
K_THREAD_STACK_DEFINE(worker_stack, WORKER_STACK);
static struct k_thread worker_thread;

/* Single producer (this ISR) and single consumer (the worker) per ring. */
static void uart_isr(const struct device *dev, void *user_data)
{
	struct camera_uart *uart = user_data;
	uint8_t chunk[16];

	uart_irq_update(dev);
	while (uart_irq_rx_ready(dev) > 0) {
		int n = uart_fifo_read(dev, chunk, sizeof(chunk));

		if (n <= 0) {
			break;
		}
		(void)ring_buf_put(&uart->ring, chunk, (uint32_t)n);
	}
	k_sem_give(&worker_wake);
}

static int uart_send(enum parking_lane_id lane, const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uarts[lane].dev, data[i]);
	}
	return 0;
}

static void wake(void)
{
	k_sem_give(&worker_wake);
}

static const struct services_io uart_io = {
	.send = uart_send,
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
		int64_t now_ms = k_uptime_get();

		for (int i = 0; i < PARKING_LANE_COUNT; i++) {
			uint8_t byte;

			while (ring_buf_get(&uarts[i].ring, &byte, 1) == 1) {
				services_receive(i, byte, now_ms);
			}
		}
		services_process(now_ms);
	}
}

int parking_services_start(void)
{
	services_init(&uart_io);
	for (int i = 0; i < PARKING_LANE_COUNT; i++) {
		struct camera_uart *uart = &uarts[i];

		if (!device_is_ready(uart->dev)) {
			return -ENODEV;
		}
		ring_buf_init(&uart->ring, sizeof(uart->storage), uart->storage);
		int rc = uart_irq_callback_user_data_set(uart->dev, uart_isr, uart);

		if (rc != 0) {
			return rc;
		}
		uart_irq_rx_enable(uart->dev);
	}
	k_thread_create(&worker_thread, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack),
			worker, NULL, NULL, NULL, K_PRIO_PREEMPT(WORKER_PRIORITY), 0, K_NO_WAIT);
	k_thread_name_set(&worker_thread, "protocols");
	return parking_register_services(&parking_services);
}
