/*
 * Tests of fsm/states: the entry lane rules, applied by the real engine
 * (fsm/lane.c) with FFF fakes in place of services/barrier and
 * services/signals. Each test starts the lane in a state, dispatches events
 * and checks the new state and which services were called, in which order.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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
static const uint8_t running_states[] = {
	STATE_INIT,    STATE_IDLE,    STATE_IDENTIFYING, STATE_OPENING, STATE_OPEN,
	STATE_PASSING, STATE_CLOSING, STATE_ASSIST,
};

#define READ_PLATE "ABC1D23"

/* Deadline checks bound the arming instant, as in tests/unit/fsm. */
#define OPENING_BUDGET_MS 2500
#define PASSAGE_BUDGET_MS 10000
#define CLOSING_BUDGET_MS 2500
#define FAR_FUTURE_MS     (INT64_MAX / 2)

/*
 * sessions_can_enter fake: FFF keeps only the pointer, which points into the
 * guard's stack, so the plate is copied while it is still valid.
 */
static char asked_plate[PLATE_LEN + 1];
static bool may_enter_answer;

static bool record_asked_plate(const char *plate)
{
	strncpy(asked_plate, plate, PLATE_LEN);
	return may_enter_answer;
}

/* sessions_create fake: copies the plate (FFF keeps only the pointer). */
static char created_plate[PLATE_LEN + 1];
static int64_t created_entry_ms;

static int record_created_session(const char *plate, int64_t entry_ms)
{
	strncpy(created_plate, plate, PLATE_LEN);
	created_entry_ms = entry_ms;
	return 0;
}

/* camera_plate fake: the camera read READ_PLATE. */
static int plate_was_read(enum lane_id id, char *plate)
{
	ARG_UNUSED(id);
	strcpy(plate, READ_PLATE);
	return 0;
}

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

/* Position of a fake in the call history, or -1 if it was not called. */
static int call_position(void *fake_fn)
{
	for (unsigned int i = 0; i < fff.call_history_idx; i++) {
		if (fff.call_history[i] == fake_fn) {
			return (int)i;
		}
	}
	return -1;
}

/*
 * REQ-09: exactly one assistance, motor stopped before the signals change,
 * and the recognition dropped.
 */
static void assert_assisted_once(void)
{
	zassert_equal(barrier_stop_fake.call_count, 1);
	zassert_equal(signals_assist_fake.call_count, 1);
	zassert_equal(barrier_stop_fake.arg0_val, LANE_ENTRY);
	zassert_equal(signals_assist_fake.arg0_val, LANE_ENTRY);
	zassert_true(call_position(barrier_stop) < call_position(signals_assist),
		     "motor first");
	zassert_equal(camera_abandon_fake.call_count, 1, "recognition dropped");
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

/* ---- Identification (REQ-02, REQ-03, REQ-04) ---- */

ZTEST(fsm_states, test_arrival_at_closed_barrier_starts_recognition)
{
	start_in(STATE_IDLE);
	barrier_is_closed_fake.return_val = true;

	zassert_true(lane_dispatch(&lane, EV_CAR_ARRIVED));

	zassert_equal(lane_state(&lane), STATE_IDENTIFYING);
	zassert_equal(camera_start_fake.call_count, 1);
	zassert_equal(camera_start_fake.arg0_val, LANE_ENTRY);
}

/* REQ-01: IDLE without the lower limit asks for assistance, never homing. */
ZTEST(fsm_states, test_arrival_without_closed_barrier_latches_assistance)
{
	start_in(STATE_IDLE);
	barrier_is_closed_fake.return_val = false;

	zassert_true(lane_dispatch(&lane, EV_CAR_ARRIVED));

	zassert_equal(lane_state(&lane), STATE_ASSIST_LOCKED);
	zassert_equal(camera_start_fake.call_count, 0);
	assert_assisted_once();
}

ZTEST(fsm_states, test_accepted_plate_opens_the_barrier)
{
	start_in(STATE_IDENTIFYING);
	camera_plate_fake.custom_fake = plate_was_read;
	sessions_can_enter_fake.custom_fake = record_asked_plate;
	may_enter_answer = true;
	memset(asked_plate, 0, sizeof(asked_plate));

	zassert_true(lane_dispatch(&lane, EV_PLATE_READ));

	zassert_equal(lane_state(&lane), STATE_OPENING);
	zassert_equal(camera_plate_fake.arg0_val, LANE_ENTRY);
	zassert_str_equal(asked_plate, READ_PLATE, "sessions asked about the read plate");
	zassert_equal(barrier_open_fake.call_count, 1);
	zassert_equal(barrier_open_fake.arg0_val, LANE_ENTRY);
	zassert_equal(signals_assist_fake.call_count, 0);
}

ZTEST(fsm_states, test_refused_plate_asks_for_assistance)
{
	start_in(STATE_IDENTIFYING);
	camera_plate_fake.custom_fake = plate_was_read;
	sessions_can_enter_fake.return_val = false; /* lot full or already parked */

	zassert_true(lane_dispatch(&lane, EV_PLATE_READ));

	zassert_equal(lane_state(&lane), STATE_ASSIST);
	zassert_equal(barrier_open_fake.call_count, 0, "never opens");
	assert_assisted_once();
}

ZTEST(fsm_states, test_plate_read_without_plate_asks_for_assistance)
{
	start_in(STATE_IDENTIFYING);
	camera_plate_fake.return_val = -ENOENT;

	zassert_true(lane_dispatch(&lane, EV_PLATE_READ));

	zassert_equal(lane_state(&lane), STATE_ASSIST);
	zassert_equal(sessions_can_enter_fake.call_count, 0);
	zassert_equal(barrier_open_fake.call_count, 0, "never opens");
}

ZTEST(fsm_states, test_failed_capture_asks_for_assistance)
{
	start_in(STATE_IDENTIFYING);

	zassert_true(lane_dispatch(&lane, EV_CAPTURE_FAILED));

	zassert_equal(lane_state(&lane), STATE_ASSIST);
	assert_assisted_once();
}

ZTEST(fsm_states, test_car_leaving_during_recognition_abandons_it)
{
	start_in(STATE_IDENTIFYING);

	zassert_true(lane_dispatch(&lane, EV_CAR_LEFT));

	zassert_equal(lane_state(&lane), STATE_IDLE);
	zassert_equal(camera_abandon_fake.call_count, 1);
	zassert_equal(camera_abandon_fake.arg0_val, LANE_ENTRY);
	zassert_equal(signals_assist_fake.call_count, 0, "no assistance");
}

/* ---- Opening (REQ-06) ---- */

ZTEST(fsm_states, test_opening_arms_the_motion_budget)
{
	start_in(STATE_IDENTIFYING);
	camera_plate_fake.custom_fake = plate_was_read;
	sessions_can_enter_fake.return_val = true;
	int64_t before_ms = k_uptime_get();

	zassert_true(lane_dispatch(&lane, EV_PLATE_READ));
	int64_t after_ms = k_uptime_get();

	zassert_false(lane_deadline_expired(&lane, before_ms + OPENING_BUDGET_MS - 1));
	zassert_true(lane_deadline_expired(&lane, after_ms + OPENING_BUDGET_MS));
}

ZTEST(fsm_states, test_opening_timeout_latches_assistance)
{
	start_in(STATE_OPENING);

	zassert_true(lane_dispatch(&lane, EV_TIMEOUT));

	zassert_equal(lane_state(&lane), STATE_ASSIST_LOCKED);
	assert_assisted_once();
}

/* ---- Rearm (REQ-09) ---- */

ZTEST(fsm_states, test_assistance_rearms_when_car_leaves_with_beam_clear)
{
	start_in(STATE_ASSIST);
	presence_beam_active_fake.return_val = false;

	zassert_true(lane_dispatch(&lane, EV_CAR_LEFT));

	zassert_equal(lane_state(&lane), STATE_IDLE);
	zassert_equal(signals_stop_fake.call_count, 1);
}

ZTEST(fsm_states, test_assistance_waits_for_the_beam_when_car_leaves)
{
	start_in(STATE_ASSIST);
	presence_beam_active_fake.return_val = true;

	zassert_false(lane_dispatch(&lane, EV_CAR_LEFT));

	zassert_equal(lane_state(&lane), STATE_ASSIST);
}

ZTEST(fsm_states, test_assistance_rearms_when_beam_clears_with_car_far)
{
	start_in(STATE_ASSIST);
	presence_car_far_fake.return_val = true;

	zassert_true(lane_dispatch(&lane, EV_BEAM_OFF));

	zassert_equal(lane_state(&lane), STATE_IDLE);
}

ZTEST(fsm_states, test_assistance_waits_for_the_car_when_beam_clears)
{
	start_in(STATE_ASSIST);
	presence_car_far_fake.return_val = false;

	zassert_false(lane_dispatch(&lane, EV_BEAM_OFF));

	zassert_equal(lane_state(&lane), STATE_ASSIST);
}

/* ---- Open and passing (REQ-06, REQ-07) ---- */

ZTEST(fsm_states, test_upper_limit_turns_green_on)
{
	start_in(STATE_OPENING);

	zassert_true(lane_dispatch(&lane, EV_UPPER_REACHED));

	zassert_equal(lane_state(&lane), STATE_OPEN);
	zassert_equal(signals_go_fake.call_count, 1);
	zassert_equal(signals_go_fake.arg0_val, LANE_ENTRY);
	zassert_equal(barrier_stop_fake.call_count, 0, "the barrier already stopped");
}

/* REQ-06: from OPENING, no other event may turn green on. */
ZTEST(fsm_states, test_green_only_after_the_upper_limit)
{
	for (uint8_t event = 0; event < EV_COUNT; event++) {
		if (event == EV_UPPER_REACHED) {
			continue;
		}
		start_in(STATE_OPENING);
		(void)lane_dispatch(&lane, event);
	}

	zassert_equal(signals_go_fake.call_count, 0);
}

ZTEST(fsm_states, test_beam_going_active_keeps_the_passage_budget)
{
	start_in(STATE_OPENING);
	int64_t before_ms = k_uptime_get();

	zassert_true(lane_dispatch(&lane, EV_UPPER_REACHED));
	int64_t after_ms = k_uptime_get();
	zassert_true(lane_dispatch(&lane, EV_BEAM_ON));

	zassert_equal(lane_state(&lane), STATE_PASSING);
	zassert_false(lane_deadline_expired(&lane, before_ms + PASSAGE_BUDGET_MS - 1));
	zassert_true(lane_deadline_expired(&lane, after_ms + PASSAGE_BUDGET_MS),
		     "counted from the opening, not from the beam");
}

ZTEST(fsm_states, test_open_ignores_events_that_are_not_a_passage)
{
	const uint8_t not_a_passage[] = {EV_BEAM_OFF, EV_CAR_LEFT, EV_CAR_ARRIVED};

	start_in(STATE_OPEN);
	for (size_t i = 0; i < ARRAY_SIZE(not_a_passage); i++) {
		zassert_false(lane_dispatch(&lane, not_a_passage[i]), "event %u",
			      not_a_passage[i]);
	}

	zassert_equal(lane_state(&lane), STATE_OPEN);
}

ZTEST(fsm_states, test_passage_timeout_latches_assistance_from_open)
{
	start_in(STATE_OPEN);

	zassert_true(lane_dispatch(&lane, EV_TIMEOUT));

	zassert_equal(lane_state(&lane), STATE_ASSIST_LOCKED);
	assert_assisted_once();
}

ZTEST(fsm_states, test_passage_timeout_latches_assistance_from_passing)
{
	start_in(STATE_PASSING);

	zassert_true(lane_dispatch(&lane, EV_TIMEOUT));

	zassert_equal(lane_state(&lane), STATE_ASSIST_LOCKED);
	assert_assisted_once();
}

ZTEST(fsm_states, test_identifying_has_no_lane_deadline)
{
	start_in(STATE_IDLE);
	barrier_is_closed_fake.return_val = true;

	zassert_true(lane_dispatch(&lane, EV_CAR_ARRIVED)); /* IDENTIFYING */

	zassert_false(lane_deadline_expired(&lane, FAR_FUTURE_MS),
		      "the camera keeps its own deadlines");
}

/* ---- Passage completion and closing (REQ-07, REQ-08) ---- */

/* Beam active then clear, car far: the passage that closes the barrier. */
static void complete_passage_on(uint8_t last_edge)
{
	start_in(STATE_PASSING);
	camera_plate_fake.custom_fake = plate_was_read;
	sessions_create_fake.custom_fake = record_created_session;
	memset(created_plate, 0, sizeof(created_plate));
	presence_beam_active_fake.return_val = false;
	presence_car_far_fake.return_val = true;

	zassert_true(lane_dispatch(&lane, last_edge));
	zassert_equal(lane_state(&lane), STATE_CLOSING);
}

ZTEST(fsm_states, test_beam_clearing_with_car_far_completes_the_passage)
{
	complete_passage_on(EV_BEAM_OFF);
}

ZTEST(fsm_states, test_car_leaving_with_beam_clear_completes_the_passage)
{
	complete_passage_on(EV_CAR_LEFT);
}

ZTEST(fsm_states, test_beam_clearing_with_car_near_is_no_passage_yet)
{
	start_in(STATE_PASSING);
	presence_car_far_fake.return_val = false;

	zassert_false(lane_dispatch(&lane, EV_BEAM_OFF));

	zassert_equal(lane_state(&lane), STATE_PASSING);
	zassert_equal(sessions_create_fake.call_count, 0);
}

ZTEST(fsm_states, test_car_far_with_beam_still_active_is_no_passage_yet)
{
	start_in(STATE_PASSING);
	presence_beam_active_fake.return_val = true;
	presence_car_far_fake.return_val = true;

	zassert_false(lane_dispatch(&lane, EV_CAR_LEFT));

	zassert_equal(lane_state(&lane), STATE_PASSING);
}

/* REQ-07: only the completed passage creates the session. */
ZTEST(fsm_states, test_completed_passage_creates_the_session)
{
	int64_t before_ms = k_uptime_get();

	complete_passage_on(EV_BEAM_OFF);

	zassert_equal(sessions_create_fake.call_count, 1);
	zassert_str_equal(created_plate, READ_PLATE);
	zassert_true(created_entry_ms >= before_ms, "virtual time of the passage");
	zassert_true(created_entry_ms <= k_uptime_get());
}

/* REQ-08: green off and red on before the motor starts closing. */
ZTEST(fsm_states, test_closing_shows_stop_before_the_motor_moves)
{
	complete_passage_on(EV_BEAM_OFF);

	zassert_equal(barrier_close_fake.call_count, 1);
	zassert_equal(barrier_close_fake.arg0_val, LANE_ENTRY);
	zassert_true(call_position(signals_stop) < call_position(barrier_close),
		     "red before the motor");
}

ZTEST(fsm_states, test_closing_without_plate_still_closes)
{
	start_in(STATE_PASSING);
	camera_plate_fake.return_val = -ENOENT;
	presence_car_far_fake.return_val = true;

	zassert_true(lane_dispatch(&lane, EV_BEAM_OFF));

	zassert_equal(sessions_create_fake.call_count, 0);
	zassert_equal(barrier_close_fake.call_count, 1);
}

ZTEST(fsm_states, test_closing_arms_the_motion_budget)
{
	int64_t before_ms = k_uptime_get();

	complete_passage_on(EV_BEAM_OFF);
	int64_t after_ms = k_uptime_get();

	zassert_false(lane_deadline_expired(&lane, before_ms + CLOSING_BUDGET_MS - 1));
	zassert_true(lane_deadline_expired(&lane, after_ms + CLOSING_BUDGET_MS));
}

ZTEST(fsm_states, test_lower_limit_without_a_car_waits_in_idle)
{
	start_in(STATE_CLOSING);
	presence_car_near_fake.return_val = false;

	zassert_true(lane_dispatch(&lane, EV_LOWER_REACHED));

	zassert_equal(lane_state(&lane), STATE_IDLE);
	zassert_equal(camera_start_fake.call_count, 0);
}

ZTEST(fsm_states, test_lower_limit_with_the_next_car_near_identifies_it)
{
	start_in(STATE_CLOSING);
	presence_car_near_fake.return_val = true;

	zassert_true(lane_dispatch(&lane, EV_LOWER_REACHED));

	zassert_equal(lane_state(&lane), STATE_IDENTIFYING);
	zassert_equal(camera_start_fake.call_count, 1);
}

/* REQ-08: obstruction stops the motor, without reversing or restarting. */
ZTEST(fsm_states, test_obstruction_while_closing_stops_and_latches)
{
	start_in(STATE_CLOSING);

	zassert_true(lane_dispatch(&lane, EV_BEAM_ON));
	zassert_false(lane_dispatch(&lane, EV_BEAM_OFF), "beam clearing changes nothing");

	zassert_equal(lane_state(&lane), STATE_ASSIST_LOCKED);
	assert_assisted_once();
	zassert_equal(barrier_open_fake.call_count, 0, "never reverses");
	zassert_equal(barrier_close_fake.call_count, 0, "never restarts");
}

ZTEST(fsm_states, test_closing_timeout_latches_assistance)
{
	start_in(STATE_CLOSING);

	zassert_true(lane_dispatch(&lane, EV_TIMEOUT));

	zassert_equal(lane_state(&lane), STATE_ASSIST_LOCKED);
	assert_assisted_once();
}
