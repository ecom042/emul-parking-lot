#include <errno.h>

#include <zephyr/kernel.h>

#include "parking_tariff.h"
#include "parking_uart.h"
#include "payment_display.h"
#include "services.h"

enum {
	EXIT_STACK_SIZE = 2048,
	EXIT_PRIORITY = 2,
	EXIT_WAKE_MS = 10,
	TERMINAL_RX_BUDGET = 256,
};

K_SEM_DEFINE(exit_wake, 0, 1);
K_THREAD_STACK_DEFINE(exit_stack, EXIT_STACK_SIZE);
static struct k_thread exit_thread;
static int display_status;

static int show_plate(const char plate[PARKING_PLATE_SIZE])
{
	return display_status == 0 ? payment_display_show_plate(plate) : display_status;
}

static void wake(void)
{
	k_sem_give(&exit_wake);
}

static const struct services_exit_io exit_io = {
	.send = parking_terminal_send,
	.show_plate = show_plate,
	.tariff = parking_tariff,
	.show_amount = payment_display_show_amount,
	.now = k_uptime_get,
	.wake = wake,
};

static void drain_terminal(void)
{
	for (int i = 0; i < TERMINAL_RX_BUDGET; i++) {
		uint8_t byte;
		int64_t received_ms;
		int rc = parking_terminal_read(&byte, &received_ms);

		if (rc == -EOVERFLOW) {
			services_exit_resync();
		} else if (rc == 1) {
			services_exit_receive(byte, received_ms);
		} else {
			break;
		}
	}
}

static void exit_worker(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	display_status = payment_display_init();
	while (true) {
		(void)k_sem_take(&exit_wake, K_MSEC(EXIT_WAKE_MS));
		drain_terminal();
		services_exit_process();
	}
}

int parking_exit_start(void)
{
	int rc = parking_terminal_init(&exit_wake);

	if (rc != 0) {
		return rc;
	}
	services_exit_init(&exit_io);
	k_thread_create(&exit_thread, exit_stack, K_THREAD_STACK_SIZEOF(exit_stack),
			exit_worker, NULL, NULL, NULL, K_PRIO_PREEMPT(EXIT_PRIORITY), 0,
			K_NO_WAIT);
	(void)k_thread_name_set(&exit_thread, "payment");
	return 0;
}
