/*
 * Tests of fsm/states: the entry lane rules, applied by the real engine
 * (fsm/lane.c) with FFF fakes in place of services/barrier and
 * services/signals. Each test starts the lane in a state, dispatches events
 * and checks the new state and which services were called, in which order.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "../fakes/service_fakes.h"
#include "fsm/lane.h"
#include "fsm/states.h"
#include "services/barrier.h"
#include "services/events.h"
#include "services/signals.h"

/* The engine needs a queue to bind; these tests dispatch directly. */
K_MSGQ_DEFINE(test_queue, sizeof(uint8_t), 4, 1);

/* States in which the latching faults must work (all but the latched one). */
static const uint8_t running_states[] = {STATE_INIT, STATE_IDLE, STATE_ASSIST};

static struct lane lane;

static void start_in(uint8_t state)
{
	lane_init(&lane, LANE_ENTRY, states_entry_def(), &test_queue, state);
}

static void reset_fakes(void *fixture)
{
	ARG_UNUSED(fixture);
	service_fakes_reset();
	start_in(STATE_INIT);
}

static void assert_no_service_called(void)
{
	zassert_equal(fff.call_history_idx, 0, "a service was called");
}

/*
 * REQ-09: exactly one assistance, motor stopped before the signals change.
 * The assistance is the last thing that happened (a guard may come before).
 */
static void assert_assisted_once(void)
{
	unsigned int calls = fff.call_history_idx;

	zassert_equal(barrier_stop_fake.call_count, 1);
	zassert_equal(signals_assist_fake.call_count, 1);
	zassert_equal(barrier_stop_fake.arg0_val, LANE_ENTRY);
	zassert_equal(signals_assist_fake.arg0_val, LANE_ENTRY);
	zassert_true(calls >= 2);
	zassert_equal(fff.call_history[calls - 2], (void *)barrier_stop, "motor first");
	zassert_equal(fff.call_history[calls - 1], (void *)signals_assist);
}

ZTEST_SUITE(fsm_states, NULL, NULL, reset_fakes, NULL, NULL);

ZTEST(fsm_states, test_boot_with_barrier_closed_goes_idle_showing_stop)
{
	barrier_is_closed_fake.return_val = true;

	zassert_true(lane_dispatch(&lane, EV_BOOT));

	zassert_equal(lane_state(&lane), STATE_IDLE);
	zassert_equal(barrier_is_closed_fake.arg0_val, LANE_ENTRY);
	zassert_equal(signals_stop_fake.call_count, 1);
	zassert_equal(signals_stop_fake.arg0_val, LANE_ENTRY);
	zassert_equal(barrier_stop_fake.call_count, 0, "no motor action");
	zassert_equal(signals_assist_fake.call_count, 0, "no assistance");
}

ZTEST(fsm_states, test_boot_with_barrier_not_closed_latches_assistance)
{
	barrier_is_closed_fake.return_val = false;

	zassert_true(lane_dispatch(&lane, EV_BOOT));

	zassert_equal(lane_state(&lane), STATE_ASSIST_LOCKED);
	assert_assisted_once();
	zassert_equal(signals_stop_fake.call_count, 0);
}

ZTEST(fsm_states, test_limits_conflict_latches_from_every_running_state)
{
	for (size_t i = 0; i < ARRAY_SIZE(running_states); i++) {
		reset_fakes(NULL);
		start_in(running_states[i]);

		zassert_true(lane_dispatch(&lane, EV_LIMITS_CONFLICT));

		zassert_equal(lane_state(&lane), STATE_ASSIST_LOCKED, "from %u",
			      running_states[i]);
		assert_assisted_once();
	}
}

ZTEST(fsm_states, test_adc_fault_latches_from_every_running_state)
{
	for (size_t i = 0; i < ARRAY_SIZE(running_states); i++) {
		reset_fakes(NULL);
		start_in(running_states[i]);

		zassert_true(lane_dispatch(&lane, EV_ADC_FAULT));

		zassert_equal(lane_state(&lane), STATE_ASSIST_LOCKED, "from %u",
			      running_states[i]);
		assert_assisted_once();
	}
}

/* REQ-01: a fault cleared later must not unlock the lane, nor buzz again. */
ZTEST(fsm_states, test_latched_assistance_ignores_every_event)
{
	start_in(STATE_ASSIST_LOCKED);
	barrier_is_closed_fake.return_val = true;

	for (uint8_t event = 0; event < EV_COUNT; event++) {
		zassert_false(lane_dispatch(&lane, event), "event %u", event);
	}

	zassert_equal(lane_state(&lane), STATE_ASSIST_LOCKED);
	assert_no_service_called();
}

ZTEST(fsm_states, test_idle_ignores_a_second_boot)
{
	start_in(STATE_IDLE);

	zassert_false(lane_dispatch(&lane, EV_BOOT));

	zassert_equal(lane_state(&lane), STATE_IDLE);
	assert_no_service_called();
}

ZTEST(fsm_states, test_assistance_action_keeps_going_after_a_gpio_error)
{
	barrier_stop_fake.return_val = -EIO;

	zassert_true(lane_dispatch(&lane, EV_LIMITS_CONFLICT));

	zassert_equal(lane_state(&lane), STATE_ASSIST_LOCKED);
	zassert_equal(signals_assist_fake.call_count, 1, "signals still asked");
}
