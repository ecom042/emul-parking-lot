#include "fsm/runner.h"

#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "fsm/lanes.h"
#include "fsm/states.h"
#include "services/barrier.h"
#include "services/camera.h"
#include "services/distance.h"
#include "services/events.h"
#include "services/payment.h"
#include "services/presence.h"
#include "services/screen.h"
#include "services/sessions.h"
#include "services/signals.h"

/*
 * One lane turn waits at most this long for an event, so the UARTs are
 * serviced and deadlines are checked about every millisecond: replies arrive
 * in 8-byte pieces every 10 ms, and a motion deadline may be late by 10 ms.
 */
#define LANE_EVENT_WAIT_MS 1

/* Room for the entry actions and the services they call; no big locals. */
#define LANE_STACK_SIZE 2048

/*
 * The equipment refreshes its inputs every 10 ms tick. Right after reset the
 * limit switches may not show their level yet, and a closed barrier would
 * read as "not closed" (latched assistance). Two ticks, with the outputs
 * already safe, before the lanes decide anything from the inputs.
 */
#define INPUT_SETTLE_MS 20

/* Preemptive priorities; lower is more urgent. Entry is the more urgent lane. */
#define ENTRY_THREAD_PRIORITY 5
#define EXIT_THREAD_PRIORITY  6

/* What differs between the lanes; everything else is the same code. */
struct lane_setup {
	const struct lane_def *(*rules)(void); /**< the lane's state machine */
	int (*extra_init)(void);       /**< lane-only equipment; NULL = none */
	int (*blocking_init)(void);    /**< slow setup run by the lane's own
					*   thread, at its priority; NULL = none */
	void (*extra_poll)(int64_t now_ms); /**< lane-only UART service; NULL = none */
	int priority;
	const char *thread_name;
};

/* Indexed by lane id; runner_start() goes in this order (entry first). */
static const struct lane_setup setups[LANE_COUNT] = {
	[LANE_ENTRY] = {
		.rules = states_entry_def,
		.priority = ENTRY_THREAD_PRIORITY,
		.thread_name = "lane_entry",
	},
	[LANE_EXIT] = {
		.rules = states_exit_def,
		.extra_init = payment_init,
		.blocking_init = screen_init, /* about 400 ms of display reset */
		.extra_poll = payment_poll,
		.priority = EXIT_THREAD_PRIORITY,
		.thread_name = "lane_exit",
	},
};

K_THREAD_STACK_ARRAY_DEFINE(lane_stacks, LANE_COUNT, LANE_STACK_SIZE);

static struct {
	struct k_thread threads[LANE_COUNT]; /**< one per lane */
	bool ready[LANE_COUNT];              /**< set up and EV_BOOT queued */
	bool distance_ready;                 /**< shared ADC4 set up */
} self;

static int init_services(enum lane_id id)
{
	int err = barrier_init(id);

	if (err != 0) {
		return err;
	}
	err = signals_init(id);
	if (err != 0) {
		return err;
	}
	err = presence_init(id);
	if (err != 0) {
		return err;
	}
	err = camera_init(id);
	if (err != 0 || setups[id].extra_init == NULL) {
		return err;
	}
	return setups[id].extra_init();
}

/* REQ-09 as far as the failed equipment allows: a lane that could not start
 * must never look ready to let a car through.
 */
static void fall_back_to_assistance(enum lane_id id)
{
	(void)barrier_stop(id);
	(void)signals_assist(id);
}

static void report_lane_failure(enum lane_id id, int err)
{
	printk("PARKING_INIT_FAILED: lane %d: %d\n", id, err);
	fall_back_to_assistance(id);
}

static int init_lane_equipment(enum lane_id id)
{
	int err = init_services(id);

	if (err != 0) {
		report_lane_failure(id, err);
	}
	return err;
}

/*
 * The ADC4 is shared, so its failure is a fault of both lanes, latched until
 * reset (REQ-10). A lane that already failed its own setup was assisted then.
 */
static int init_distance(const int lane_err[LANE_COUNT])
{
	int err = distance_init();

	if (err == 0) {
		return 0;
	}
	printk("PARKING_INIT_FAILED: distance: %d\n", err);
	for (int id = 0; id < LANE_COUNT; id++) {
		if (lane_err[id] == 0) {
			fall_back_to_assistance(id);
		}
	}
	return err;
}

/* Reported in setup order: entry lane, exit lane, then distance. */
static int first_error(const int lane_err[LANE_COUNT], int distance_err)
{
	for (int id = 0; id < LANE_COUNT; id++) {
		if (lane_err[id] != 0) {
			return lane_err[id];
		}
	}
	return distance_err;
}

/* The queue is static, so EV_BOOT simply waits for the thread to start. */
static void prepare_lane(enum lane_id id)
{
	struct lane *lane = lanes_get(id);

	lane_init(lane, id, setups[id].rules(), lane->queue, STATE_INIT);
	(void)events_post(id, EV_BOOT);
}

int runner_init(void)
{
	int lane_err[LANE_COUNT];

	sessions_clear(); /* REQ-13: every run starts with an empty lot */

	/* Lanes first: the distance fallback needs their signals configured. */
	for (int id = 0; id < LANE_COUNT; id++) {
		lane_err[id] = init_lane_equipment(id);
	}
	int distance_err = init_distance(lane_err);

	self.distance_ready = (distance_err == 0);
	k_msleep(INPUT_SETTLE_MS);
	for (int id = 0; id < LANE_COUNT; id++) {
		self.ready[id] = self.distance_ready && (lane_err[id] == 0);
		if (self.ready[id]) {
			prepare_lane(id);
		}
	}
	return first_error(lane_err, distance_err);
}

void runner_step(struct lane *lane)
{
	enum lane_id id = (enum lane_id)lane->id;

	(void)lane_process(lane, K_MSEC(LANE_EVENT_WAIT_MS));

	int64_t now_ms = k_uptime_get();

	if (lane_deadline_expired(lane, now_ms)) {
		(void)lane_dispatch(lane, EV_TIMEOUT);
	}
	camera_poll(id, now_ms);
	if (setups[id].extra_poll != NULL) {
		setups[id].extra_poll(now_ms);
	}
}

/*
 * A slow setup (the exit display: resets, delays and a long SPI fill) runs in
 * the lane's own thread, at that lane's priority: in main() (priority 0) it
 * would starve the more urgent entry lane while the SPI transfer runs.
 */
static int finish_blocking_init(enum lane_id id)
{
	if (setups[id].blocking_init == NULL) {
		return 0;
	}

	int err = setups[id].blocking_init();

	if (err != 0) {
		report_lane_failure(id, err);
	}
	return err;
}

/* A lane whose slow setup failed is left in assistance: its thread ends. */
static void run_lane(void *lane_ptr, void *unused1, void *unused2)
{
	struct lane *lane = lane_ptr;

	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);

	if (finish_blocking_init((enum lane_id)lane->id) != 0) {
		return;
	}
	for (;;) {
		runner_step(lane);
	}
}

static void start_lane_thread(enum lane_id id)
{
	struct k_thread *thread = &self.threads[id];

	(void)k_thread_create(thread, lane_stacks[id], K_THREAD_STACK_SIZEOF(lane_stacks[id]),
			      run_lane, lanes_get(id), NULL, NULL,
			      K_PRIO_PREEMPT(setups[id].priority), 0, K_NO_WAIT);
	k_thread_name_set(thread, setups[id].thread_name);
}

/* EV_BOOT is already queued, so each lane leaves INIT before its first
 * distance event.
 */
void runner_start(void)
{
	for (int id = 0; id < LANE_COUNT; id++) {
		if (self.ready[id]) {
			start_lane_thread(id);
		}
	}
	if (self.distance_ready) {
		distance_start();
	}
}
