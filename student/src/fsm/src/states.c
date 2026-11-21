#include "fsm/states.h"

#include "services/barrier.h"
#include "services/camera.h"
#include "services/events.h"
#include "services/presence.h"
#include "services/sessions.h"
#include "services/signals.h"

/* REQ-06: opening or closing must reach its limit switch within this. */
#define BARRIER_MOTION_BUDGET_MS 2500

/* Table cell shortcuts: GO always moves; GO_IF asks the guard first. */
#define GO(target) {.next = (target)}
#define GO_IF(guard_fn, target, else_target)                                   \
	{.next = (target), .guard = (guard_fn), .otherwise = (else_target)}

/*
 * Faults that need a reset (REQ-01, REQ-09): contradictory limits and ADC
 * faults latch assistance from every state, so every row lists them.
 */
#define LATCHING_FAULTS                                                        \
	[EV_LIMITS_CONFLICT] = GO(STATE_ASSIST_LOCKED),                        \
	[EV_ADC_FAULT] = GO(STATE_ASSIST_LOCKED)

static enum lane_id lane_id_of(const struct lane *lane)
{
	return (enum lane_id)lane->id;
}

/* ---- Guards: read the lane and the services, change nothing ---- */

/* Closed means lower limit active and upper inactive, so contradictory
 * limits are never "closed".
 */
static bool barrier_closed(const struct lane *lane)
{
	return barrier_is_closed(lane_id_of(lane));
}

/* REQ-04: the plate just read has no session and a spot is free. */
static bool plate_may_enter(const struct lane *lane)
{
	char plate[PLATE_LEN + 1];

	if (camera_plate(lane_id_of(lane), plate) != 0) {
		return false;
	}
	return sessions_can_enter(plate);
}

static bool beam_clear(const struct lane *lane)
{
	return !presence_beam_active(lane_id_of(lane));
}

static bool car_far(const struct lane *lane)
{
	return presence_car_far(lane_id_of(lane));
}

/* ---- Entry actions ---- */

/* Entry actions return nothing: a GPIO error has no caller to go back to.
 * A barrier that did not move is caught by the motion deadline.
 */

/* The engine has no exit actions, so every state reached from
 * IDENTIFYING without a result drops the recognition on entry.
 */
static void wait_for_car(struct lane *lane)
{
	camera_abandon(lane_id_of(lane));
	(void)signals_stop(lane_id_of(lane));
}

/* Cannot be busy: every way out of IDENTIFYING ends or abandons the
 * recognition before the lane can come back here.
 */
static void start_recognition(struct lane *lane)
{
	(void)camera_start(lane_id_of(lane));
}

/* The barrier stops itself at the upper limit and only then posts
 * EV_UPPER_REACHED, so green can never come with the motor on (REQ-06).
 */
static void open_barrier(struct lane *lane)
{
	(void)barrier_open(lane_id_of(lane));
}

/* REQ-09: motor off first, then red on, green off and one buzzer pulse. */
static void request_assistance(struct lane *lane)
{
	(void)barrier_stop(lane_id_of(lane));
	camera_abandon(lane_id_of(lane));
	(void)signals_assist(lane_id_of(lane));
}

/* ---- Entry lane rules ---- */

/*
 * REQ-01: boot goes to IDLE only with the barrier closed; any other position
 * asks for assistance instead of closing by itself (no automatic homing).
 * REQ-09: STATE_ASSIST rearms when the car is far AND the beam is clear;
 * either may come last, so both edges ask for the other condition.
 * STATE_ASSIST_LOCKED has an empty row: only a reset leaves it.
 */
static const struct lane_transition entry_table[STATE_COUNT][EV_COUNT] = {
	[STATE_INIT] = {
		LATCHING_FAULTS,
		[EV_BOOT] = GO_IF(barrier_closed, STATE_IDLE, STATE_ASSIST_LOCKED),
	},
	[STATE_IDLE] = {
		LATCHING_FAULTS,
		[EV_CAR_ARRIVED] = GO_IF(barrier_closed, STATE_IDENTIFYING,
					 STATE_ASSIST_LOCKED),
	},
	[STATE_IDENTIFYING] = {
		LATCHING_FAULTS,
		[EV_PLATE_READ] = GO_IF(plate_may_enter, STATE_OPENING, STATE_ASSIST),
		[EV_CAPTURE_FAILED] = GO(STATE_ASSIST),
		[EV_CAR_LEFT] = GO(STATE_IDLE), /* REQ-03: gave up, no assistance */
	},
	[STATE_OPENING] = {
		LATCHING_FAULTS,
		[EV_TIMEOUT] = GO(STATE_ASSIST_LOCKED), /* arm stopped halfway */
	},
	[STATE_ASSIST] = {
		LATCHING_FAULTS,
		[EV_CAR_LEFT] = GO_IF(beam_clear, STATE_IDLE, LANE_STATE_NONE),
		[EV_BEAM_OFF] = GO_IF(car_far, STATE_IDLE, LANE_STATE_NONE),
	},
};

static const lane_enter_fn entry_actions[STATE_COUNT] = {
	[STATE_IDLE] = wait_for_car,
	[STATE_IDENTIFYING] = start_recognition,
	[STATE_OPENING] = open_barrier,
	[STATE_ASSIST] = request_assistance,
	[STATE_ASSIST_LOCKED] = request_assistance,
};

static const uint16_t entry_deadlines_ms[STATE_COUNT] = {
	[STATE_OPENING] = BARRIER_MOTION_BUDGET_MS,
};

static const struct lane_def entry_def = {
	.table = &entry_table[0][0],
	.on_enter = entry_actions,
	.deadline_ms = entry_deadlines_ms,
	.n_states = STATE_COUNT,
	.n_events = EV_COUNT,
};

const struct lane_def *states_entry_def(void)
{
	return &entry_def;
}
