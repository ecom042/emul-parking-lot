#include <errno.h>

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "parking_tariff.h"
#include "parking_uart.h"
#include "payment_display.h"
#include "services.h"

DEFINE_FFF_GLOBALS;

/* UART, display, payment services and the thread API are replaced by fakes. */
FAKE_VALUE_FUNC(int, parking_terminal_init, struct k_sem *);
FAKE_VALUE_FUNC(int, parking_terminal_read, uint8_t *, int64_t *);
FAKE_VALUE_FUNC(int, parking_terminal_send, const uint8_t *, size_t);
FAKE_VALUE_FUNC(int, payment_display_init);
FAKE_VALUE_FUNC(int, payment_display_show_plate, const char *);
FAKE_VALUE_FUNC(int, payment_display_show_amount, uint32_t);
FAKE_VALUE_FUNC(int, parking_tariff, int64_t, int64_t, uint32_t *);
FAKE_VOID_FUNC(services_exit_init, const struct services_exit_io *);
FAKE_VOID_FUNC(services_exit_receive, uint8_t, int64_t);
FAKE_VOID_FUNC(services_exit_resync);
FAKE_VOID_FUNC(services_exit_process);
FAKE_VALUE_FUNC(k_tid_t, create_worker, struct k_thread *, k_thread_stack_t *, size_t,
		k_thread_entry_t, void *, void *, void *, int, uint32_t, k_timeout_t);
FAKE_VALUE_FUNC(int, name_worker, k_tid_t, const char *);

#define k_thread_create create_worker
#define k_thread_name_set name_worker
#include "../../../src/parking_exit.c"
#undef k_thread_create
#undef k_thread_name_set

/* Scripted terminal transport: one entry per parking_terminal_read() call. */
struct rx_step {
	int rc;
	uint8_t byte;
	int64_t received_ms;
};

static const struct rx_step *script;
static size_t script_len;
static char order[8];
static size_t order_len;

static int scripted_read(uint8_t *byte, int64_t *received_ms)
{
	size_t i = parking_terminal_read_fake.call_count - 1;

	if (i >= script_len) {
		return 0;
	}
	if (script[i].rc == 1) {
		*byte = script[i].byte;
		*received_ms = script[i].received_ms;
	}
	return script[i].rc;
}

static int endless_read(uint8_t *byte, int64_t *received_ms)
{
	*byte = 'x';
	*received_ms = 1;
	return 1;
}

static void note_resync(void)
{
	if (order_len < sizeof(order)) {
		order[order_len++] = 'L';
	}
}

static void note_receive(uint8_t byte, int64_t received_ms)
{
	ARG_UNUSED(received_ms);
	if (order_len < sizeof(order)) {
		order[order_len++] = (char)byte;
	}
}

static void reset_exit(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(parking_terminal_init);
	RESET_FAKE(parking_terminal_read);
	RESET_FAKE(payment_display_show_plate);
	RESET_FAKE(services_exit_init);
	RESET_FAKE(services_exit_receive);
	RESET_FAKE(services_exit_resync);
	RESET_FAKE(create_worker);
	RESET_FAKE(name_worker);
	FFF_RESET_HISTORY();
	create_worker_fake.return_val = &exit_thread;
	services_exit_resync_fake.custom_fake = note_resync;
	services_exit_receive_fake.custom_fake = note_receive;
	display_status = 0;
	script = NULL;
	script_len = 0;
	order_len = 0;
	k_sem_reset(&exit_wake);
}

ZTEST(parking_exit, test_start_stops_when_terminal_cannot_open)
{
	parking_terminal_init_fake.return_val = -ENODEV;

	zassert_equal(parking_exit_start(), -ENODEV);
	zassert_equal_ptr(parking_terminal_init_fake.arg0_val, &exit_wake);
	zassert_equal(services_exit_init_fake.call_count, 0, "no payment without a terminal");
	zassert_equal(create_worker_fake.call_count, 0, "no worker without a terminal");
}

ZTEST(parking_exit, test_start_wires_services_and_preemptible_worker)
{
	zassert_equal(parking_exit_start(), 0);

	zassert_equal(services_exit_init_fake.call_count, 1);
	const struct services_exit_io *io = services_exit_init_fake.arg0_val;

	zassert_equal_ptr(io, &exit_io);
	zassert_equal_ptr(io->send, parking_terminal_send);
	zassert_equal_ptr(io->show_plate, show_plate);
	zassert_equal_ptr(io->tariff, parking_tariff);
	zassert_equal_ptr(io->show_amount, payment_display_show_amount);

	zassert_equal(create_worker_fake.call_count, 1);
	zassert_equal_ptr(create_worker_fake.arg3_val, exit_worker);
	zassert_equal(create_worker_fake.arg7_val, K_PRIO_PREEMPT(EXIT_PRIORITY),
		      "the display must never preempt the cooperative controller");
	zassert_equal(name_worker_fake.call_count, 1);
	zassert_str_equal(name_worker_fake.arg1_val, "payment");

	io->wake();
	zassert_equal(k_sem_count_get(&exit_wake), 1);
}

ZTEST(parking_exit, test_plate_is_refused_after_display_init_failure)
{
	display_status = -EIO;

	zassert_equal(show_plate("ABC1D23"), -EIO);
	zassert_equal(payment_display_show_plate_fake.call_count, 0,
		      "an uninitialized panel must not receive pixels");
}

ZTEST(parking_exit, test_plate_is_drawn_when_display_is_ready)
{
	payment_display_show_plate_fake.return_val = -EINVAL;

	zassert_equal(show_plate("ABC1D23"), -EINVAL, "the draw result reaches the payment logic");
	zassert_equal(payment_display_show_plate_fake.call_count, 1);
	zassert_str_equal(payment_display_show_plate_fake.arg0_val, "ABC1D23");
}

ZTEST(parking_exit, test_drain_reports_loss_before_next_bytes)
{
	static const struct rx_step steps[] = {
		{ .rc = 1, .byte = 'A', .received_ms = 5 },
		{ .rc = -EOVERFLOW },
		{ .rc = 1, .byte = 'B', .received_ms = 9 },
		{ .rc = 0 },
		{ .rc = 1, .byte = 'C', .received_ms = 10 },
	};

	script = steps;
	script_len = ARRAY_SIZE(steps);
	parking_terminal_read_fake.custom_fake = scripted_read;

	drain_terminal();

	zassert_equal(parking_terminal_read_fake.call_count, 4, "an empty queue ends the drain");
	zassert_equal(services_exit_resync_fake.call_count, 1);
	zassert_equal(services_exit_receive_fake.call_count, 2);
	zassert_equal(order_len, 3);
	zassert_mem_equal(order, "ALB", 3, "the parser resynchronizes between A and B");
	zassert_equal(services_exit_receive_fake.arg1_history[0], 5);
	zassert_equal(services_exit_receive_fake.arg1_history[1], 9, "arrival time is preserved");
}

ZTEST(parking_exit, test_drain_is_bounded_per_wake)
{
	parking_terminal_read_fake.custom_fake = endless_read;

	drain_terminal();

	zassert_equal(parking_terminal_read_fake.call_count, TERMINAL_RX_BUDGET);
	zassert_equal(services_exit_receive_fake.call_count, TERMINAL_RX_BUDGET,
		      "a flooding terminal cannot keep the worker from payment timeouts");
}

ZTEST_SUITE(parking_exit, NULL, NULL, reset_exit, NULL, NULL);
