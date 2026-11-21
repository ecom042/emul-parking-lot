#include "fsm/runner.h"

#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "fsm/lanes.h"
#include "fsm/states.h"
#include "services/barrier.h"
#include "services/camera.h"
#include "services/events.h"
#include "services/signals.h"

/*
 * One lane turn waits at most this long for an event, so camera_poll() runs
 * and deadlines are checked about every millisecond: camera replies arrive
 * in 8-byte pieces every 10 ms, and a motion deadline may be late by 10 ms.
 */
#define LANE_EVENT_WAIT_MS 1

/* Room for the entry actions and the services they call; no big locals. */
#define LANE_STACK_SIZE 2048

/* Preemptive priorities; lower is more urgent. Entry is the more urgent lane. */
#define ENTRY_THREAD_PRIORITY 5

K_THREAD_STACK_DEFINE(entry_stack, LANE_STACK_SIZE);

static struct {
	struct k_thread entry_thread; /**< runs the entry lane */
	bool entry_ready;             /**< entry set up and EV_BOOT queued */
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
	return camera_init(id);
}

/* REQ-09 as far as the failed equipment allows: a lane that could not start
 * must never look ready to let a car through.
 */
static void fall_back_to_assistance(enum lane_id id)
{
	(void)barrier_stop(id);
	(void)signals_assist(id);
}

static int init_lane_equipment(enum lane_id id)
{
	int err = init_services(id);

	if (err != 0) {
		printk("PARKING_INIT_FAILED: lane %d: %d\n", id, err);
		fall_back_to_assistance(id);
	}
	return err;
}

/* The queue is static, so EV_BOOT simply waits for the thread to start. */
static void prepare_lane(enum lane_id id, const struct lane_def *rules)
{
	struct lane *lane = lanes_get(id);

	lane_init(lane, id, rules, lane->queue, STATE_INIT);
	(void)events_post(id, EV_BOOT);
}

int runner_init(void)
{
	int entry_err = init_lane_equipment(LANE_ENTRY);
	int exit_err = init_lane_equipment(LANE_EXIT);

	self.entry_ready = (entry_err == 0);
	if (self.entry_ready) {
		prepare_lane(LANE_ENTRY, states_entry_def());
	}
	return (entry_err != 0) ? entry_err : exit_err;
}

void runner_step(struct lane *lane)
{
	(void)lane_process(lane, K_MSEC(LANE_EVENT_WAIT_MS));

	int64_t now_ms = k_uptime_get();

	if (lane_deadline_expired(lane, now_ms)) {
		(void)lane_dispatch(lane, EV_TIMEOUT);
	}
	camera_poll((enum lane_id)lane->id, now_ms);
}

static void run_lane(void *lane, void *unused1, void *unused2)
{
	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);

	for (;;) {
		runner_step(lane);
	}
}

void runner_start(void)
{
	if (!self.entry_ready) {
		return;
	}
	(void)k_thread_create(&self.entry_thread, entry_stack,
			      K_THREAD_STACK_SIZEOF(entry_stack), run_lane,
			      lanes_get(LANE_ENTRY), NULL, NULL,
			      K_PRIO_PREEMPT(ENTRY_THREAD_PRIORITY), 0, K_NO_WAIT);
	k_thread_name_set(&self.entry_thread, "lane_entry");
}
