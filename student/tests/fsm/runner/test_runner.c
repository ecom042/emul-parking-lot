/*
 * Tests of fsm/runner with FFF fakes in place of the services. fsm/lane,
 * fsm/lanes and fsm/states are the real ones, so the tests read EV_BOOT from
 * the real lane queues. runner_start() is only called when no lane will get a
 * thread: a real lane thread would keep calling the fakes during other tests.
 */
#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "../fakes/service_fakes.h"
#include "fsm/lanes.h"
#include "fsm/runner.h"
#include "fsm/states.h"
#include "services/events.h"

/* Long enough for a started lane thread to run a few turns. */
#define THREAD_SETTLE_MS 5

/* Which lane a per-lane fake fails for, and with which error. */
static enum lane_id failing_lane;
static int failing_err;

static int fail_for_one_lane(enum lane_id id)
{
	return (id == failing_lane) ? failing_err : 0;
}

static struct k_msgq *queue_of(enum lane_id id)
{
	return lanes_get(id)->queue;
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);
	service_fakes_reset();
	k_msgq_purge(queue_of(LANE_ENTRY));
	k_msgq_purge(queue_of(LANE_EXIT));
}

static void assert_queue_holds_only(enum lane_id id, enum lane_event expected)
{
	uint8_t event;

	zassert_ok(k_msgq_get(queue_of(id), &event, K_NO_WAIT), "no event");
	zassert_equal(event, expected);
	zassert_equal(k_msgq_num_used_get(queue_of(id)), 0, "only one event");
}

static void assert_assisted_only(enum lane_id id)
{
	zassert_equal(barrier_stop_fake.call_count, 1);
	zassert_equal(barrier_stop_fake.arg0_val, id);
	zassert_equal(signals_assist_fake.call_count, 1);
	zassert_equal(signals_assist_fake.arg0_val, id);
}

ZTEST_SUITE(fsm_runner, NULL, NULL, reset, NULL, NULL);

ZTEST(fsm_runner, test_init_sets_up_and_boots_both_lanes)
{
	struct lane *entry = lanes_get(LANE_ENTRY);
	struct lane *exit = lanes_get(LANE_EXIT);

	zassert_ok(runner_init());

	zassert_equal(barrier_init_fake.call_count, 2, "both lanes");
	zassert_equal(signals_init_fake.call_count, 2, "both lanes");
	zassert_equal(presence_init_fake.call_count, 2, "both lanes");
	zassert_equal(camera_init_fake.call_count, 2, "both lanes");
	zassert_equal(distance_init_fake.call_count, 1, "one shared ADC4");
	zassert_equal(sessions_clear_fake.call_count, 1, "empty lot");
	zassert_equal(payment_init_fake.call_count, 1, "exit lane only");
	zassert_equal(screen_init_fake.call_count, 0, "blocking: waits for runner_start");
	zassert_equal(entry->def, states_entry_def());
	zassert_equal(exit->def, states_exit_def());
	zassert_equal(lane_state(entry), STATE_INIT);
	zassert_equal(lane_state(exit), STATE_INIT);
	assert_queue_holds_only(LANE_ENTRY, EV_BOOT);
	assert_queue_holds_only(LANE_EXIT, EV_BOOT);
	zassert_equal(signals_assist_fake.call_count, 0, "no assistance");
}

/* The limit switches need two equipment ticks after reset before EV_BOOT. */
ZTEST(fsm_runner, test_init_lets_the_inputs_settle_before_boot)
{
	int64_t before_ms = k_uptime_get();

	zassert_ok(runner_init());

	zassert_true(k_uptime_get() - before_ms >= 20, "inputs settle for 20 ms");
}

ZTEST(fsm_runner, test_entry_barrier_failure_assists_entry_without_boot)
{
	failing_lane = LANE_ENTRY;
	failing_err = -EIO;
	barrier_init_fake.custom_fake = fail_for_one_lane;

	zassert_equal(runner_init(), -EIO);

	assert_assisted_only(LANE_ENTRY);
	zassert_equal(k_msgq_num_used_get(queue_of(LANE_ENTRY)), 0, "no EV_BOOT");
	zassert_equal(camera_init_fake.call_count, 1, "only exit got this far");
	zassert_equal(camera_init_fake.arg0_val, LANE_EXIT);
}

ZTEST(fsm_runner, test_entry_signals_failure_stops_the_setup_of_that_lane)
{
	failing_lane = LANE_ENTRY;
	failing_err = -ENODEV;
	signals_init_fake.custom_fake = fail_for_one_lane;

	zassert_equal(runner_init(), -ENODEV);

	assert_assisted_only(LANE_ENTRY);
	zassert_equal(camera_init_fake.call_count, 1, "only exit");
	zassert_equal(camera_init_fake.arg0_val, LANE_EXIT);
}

ZTEST(fsm_runner, test_entry_presence_failure_stops_the_setup_of_that_lane)
{
	failing_lane = LANE_ENTRY;
	failing_err = -EIO;
	presence_init_fake.custom_fake = fail_for_one_lane;

	zassert_equal(runner_init(), -EIO);

	assert_assisted_only(LANE_ENTRY);
	zassert_equal(camera_init_fake.call_count, 1, "only exit");
	zassert_equal(camera_init_fake.arg0_val, LANE_EXIT);
}

ZTEST(fsm_runner, test_entry_camera_failure_assists_entry)
{
	failing_lane = LANE_ENTRY;
	failing_err = -ENODEV;
	camera_init_fake.custom_fake = fail_for_one_lane;

	zassert_equal(runner_init(), -ENODEV);

	assert_assisted_only(LANE_ENTRY);
	zassert_equal(k_msgq_num_used_get(queue_of(LANE_ENTRY)), 0, "no EV_BOOT");
}

/* REQ-04: one lane's fault must not hold the other back. */
ZTEST(fsm_runner, test_exit_failure_still_boots_entry)
{
	failing_lane = LANE_EXIT;
	failing_err = -EIO;
	barrier_init_fake.custom_fake = fail_for_one_lane;

	zassert_equal(runner_init(), -EIO);

	assert_assisted_only(LANE_EXIT);
	assert_queue_holds_only(LANE_ENTRY, EV_BOOT);
	zassert_equal(k_msgq_num_used_get(queue_of(LANE_EXIT)), 0, "no EV_BOOT");
}

ZTEST(fsm_runner, test_payment_terminal_failure_assists_the_exit_only)
{
	payment_init_fake.return_val = -ENODEV;

	zassert_equal(runner_init(), -ENODEV);

	assert_assisted_only(LANE_EXIT);
	assert_queue_holds_only(LANE_ENTRY, EV_BOOT);
}

ZTEST(fsm_runner, test_entry_error_is_reported_when_both_lanes_fail)
{
	barrier_init_fake.return_val = -EIO;
	signals_init_fake.return_val = -ENODEV;

	zassert_equal(runner_init(), -EIO);

	zassert_equal(barrier_stop_fake.call_count, 2, "both lanes assisted");
	zassert_equal(signals_assist_fake.call_count, 2, "both lanes assisted");
}

/* REQ-10: the shared ADC4 failing is a latched fault of both lanes. */
ZTEST(fsm_runner, test_distance_failure_assists_both_lanes_without_boot)
{
	distance_init_fake.return_val = -ENODEV;

	zassert_equal(runner_init(), -ENODEV);

	zassert_equal(barrier_stop_fake.call_count, 2, "both lanes");
	zassert_equal(signals_assist_fake.call_count, 2, "both lanes");
	zassert_equal(k_msgq_num_used_get(queue_of(LANE_ENTRY)), 0, "no EV_BOOT");
}

ZTEST(fsm_runner, test_distance_failure_does_not_assist_a_failed_lane_twice)
{
	failing_lane = LANE_ENTRY;
	failing_err = -EIO;
	barrier_init_fake.custom_fake = fail_for_one_lane;
	distance_init_fake.return_val = -ENODEV;

	zassert_equal(runner_init(), -EIO, "lane errors are reported first");

	zassert_equal(signals_assist_fake.call_count, 2, "entry once, exit once");
	zassert_equal(signals_assist_fake.arg0_history[0], LANE_ENTRY);
	zassert_equal(signals_assist_fake.arg0_history[1], LANE_EXIT);
}

ZTEST(fsm_runner, test_start_without_ready_lanes_starts_only_distance)
{
	barrier_init_fake.return_val = -EIO;
	(void)runner_init();
	service_fakes_reset();

	runner_start();
	k_msleep(THREAD_SETTLE_MS);

	zassert_equal(camera_poll_fake.call_count, 0, "no lane thread");
	zassert_equal(screen_init_fake.call_count, 0, "only for a ready lane");
	zassert_equal(distance_start_fake.call_count, 1);
}

/*
 * The exit display fails at the start of the exit thread: the exit stays in
 * assistance and its thread ends before its loop (safe to start in a test).
 */
ZTEST(fsm_runner, test_display_failure_at_start_assists_the_exit)
{
	failing_lane = LANE_ENTRY;
	failing_err = -EIO;
	barrier_init_fake.custom_fake = fail_for_one_lane; /* keep entry threadless */
	(void)runner_init();
	service_fakes_reset();
	screen_init_fake.return_val = -EIO;

	runner_start();
	k_msleep(THREAD_SETTLE_MS);

	zassert_equal(screen_init_fake.call_count, 1);
	assert_assisted_only(LANE_EXIT);
	zassert_equal(camera_poll_fake.call_count, 0, "the thread never reached its loop");
}

ZTEST(fsm_runner, test_start_without_distance_runs_nothing)
{
	distance_init_fake.return_val = -ENODEV;
	(void)runner_init();
	service_fakes_reset();

	runner_start();
	k_msleep(THREAD_SETTLE_MS);

	zassert_equal(camera_poll_fake.call_count, 0, "no lane thread");
	zassert_equal(distance_start_fake.call_count, 0);
}

ZTEST(fsm_runner, test_step_applies_one_event_then_polls_the_camera)
{
	struct lane *entry = lanes_get(LANE_ENTRY);

	lane_init(entry, LANE_ENTRY, states_entry_def(), entry->queue, STATE_INIT);
	barrier_is_closed_fake.return_val = true;
	zassert_ok(events_post(LANE_ENTRY, EV_BOOT));
	int64_t before_ms = k_uptime_get();

	runner_step(entry);

	zassert_equal(lane_state(entry), STATE_IDLE);
	zassert_equal(camera_poll_fake.call_count, 1);
	zassert_equal(camera_poll_fake.arg0_val, LANE_ENTRY);
	zassert_true(camera_poll_fake.arg1_val >= before_ms, "virtual uptime");
	zassert_equal(fff.call_history[fff.call_history_idx - 1], (void *)camera_poll,
		      "camera after the event");
	zassert_equal(payment_poll_fake.call_count, 0, "no terminal on the entry");
}

ZTEST(fsm_runner, test_exit_step_also_polls_the_payment_terminal)
{
	struct lane *exit = lanes_get(LANE_EXIT);

	lane_init(exit, LANE_EXIT, states_exit_def(), exit->queue, STATE_IDLE);
	int64_t before_ms = k_uptime_get();

	runner_step(exit);

	zassert_equal(camera_poll_fake.arg0_val, LANE_EXIT);
	zassert_equal(payment_poll_fake.call_count, 1);
	zassert_true(payment_poll_fake.arg0_val >= before_ms, "virtual uptime");
}

ZTEST(fsm_runner, test_step_without_event_still_polls_the_camera)
{
	struct lane *entry = lanes_get(LANE_ENTRY);

	lane_init(entry, LANE_ENTRY, states_entry_def(), entry->queue, STATE_IDLE);

	runner_step(entry);

	zassert_equal(lane_state(entry), STATE_IDLE);
	zassert_equal(camera_poll_fake.call_count, 1);
	zassert_equal(camera_poll_fake.arg0_val, LANE_ENTRY);
}

/*
 * Toy rules for the deadline: EV_BOOT enters WAITING, which has a short
 * budget; EV_TIMEOUT leaves it. The entry rules get deadlines in later steps.
 */
enum { TOY_IDLE = 1, TOY_WAITING, TOY_TIMED_OUT, TOY_STATE_COUNT };

#define TOY_DEADLINE_MS 20U

static const struct lane_transition toy_table[TOY_STATE_COUNT][EV_COUNT] = {
	[TOY_IDLE][EV_BOOT] = {.next = TOY_WAITING},
	[TOY_WAITING][EV_TIMEOUT] = {.next = TOY_TIMED_OUT},
};

static const lane_enter_fn toy_actions[TOY_STATE_COUNT];

static const uint16_t toy_deadlines_ms[TOY_STATE_COUNT] = {
	[TOY_WAITING] = TOY_DEADLINE_MS,
};

static const struct lane_def toy_def = {
	.table = &toy_table[0][0],
	.on_enter = toy_actions,
	.deadline_ms = toy_deadlines_ms,
	.n_states = TOY_STATE_COUNT,
	.n_events = EV_COUNT,
};

ZTEST(fsm_runner, test_step_applies_timeout_only_after_the_deadline)
{
	struct lane *entry = lanes_get(LANE_ENTRY);

	lane_init(entry, LANE_ENTRY, &toy_def, entry->queue, TOY_IDLE);
	zassert_true(lane_dispatch(entry, EV_BOOT));

	runner_step(entry);
	zassert_equal(lane_state(entry), TOY_WAITING, "deadline not reached yet");

	k_msleep(TOY_DEADLINE_MS);
	runner_step(entry);
	zassert_equal(lane_state(entry), TOY_TIMED_OUT);
	zassert_equal(camera_poll_fake.call_count, 2, "camera serviced every turn");
}
