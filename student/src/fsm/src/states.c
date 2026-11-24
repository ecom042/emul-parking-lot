#include "fsm/states.h"

#include <errno.h>

#include <zephyr/kernel.h>

#include "services/barrier.h"
#include "services/camera.h"
#include "services/events.h"
#include "services/payment.h"
#include "services/presence.h"
#include "services/screen.h"
#include "services/sessions.h"
#include "services/signals.h"
#include "services/tariff.h"

/* REQ-06: opening or closing must reach its limit switch within this. */
#define BARRIER_MOTION_BUDGET_MS 2500

/* REQ-07: from full opening to a completed passage (OPEN, then PASSING). */
#define PASSAGE_BUDGET_MS 10000

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

/* REQ-04, exit: only a plate with an active session may pay and leave. */
static bool plate_has_session(const struct lane *lane)
{
	char plate[PLATE_LEN + 1];
	int64_t entry_ms;

	if (camera_plate(lane_id_of(lane), plate) != 0) {
		return false;
	}
	return sessions_entry_time(plate, &entry_ms) == 0;
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

/* The next car may arrive while the barrier closes; its EV_CAR_ARRIVED is
 * ignored then, so the closed barrier checks the presence level instead.
 */
static bool car_near(const struct lane *lane)
{
	return presence_car_near(lane_id_of(lane));
}

/* REQ-07: the beam went active (PASSING) and is now clear, with the car far. */
static bool passage_complete(const struct lane *lane)
{
	return beam_clear(lane) && car_far(lane);
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

/* REQ-06: reached only on EV_UPPER_REACHED, which the barrier posts after
 * stopping the motor, so green never shows with the motor on.
 */
static void let_car_through(struct lane *lane)
{
	(void)signals_go(lane_id_of(lane));
}

/*
 * REQ-07: only a completed passage changes the sessions: the entry creates
 * one, the paid exit removes it. The camera still holds the plate: the next
 * recognition starts after IDLE. Neither can be refused here: the guard into
 * the cycle checked it, and each lane is the only one doing its change.
 */
static void record_entry(enum lane_id id)
{
	char plate[PLATE_LEN + 1];

	if (camera_plate(id, plate) != 0) {
		return;
	}
	(void)sessions_create(plate, k_uptime_get());
}

static void record_exit(enum lane_id id)
{
	char plate[PLATE_LEN + 1];

	if (camera_plate(id, plate) != 0) {
		return;
	}
	(void)sessions_remove(plate);
}

/* REQ-08: green off and red on before the motor moves; the guard into
 * CLOSING already checked that the beam is clear.
 */
static void close_barrier(enum lane_id id)
{
	(void)signals_stop(id);
	(void)barrier_close(id);
}

static void close_after_entry(struct lane *lane)
{
	record_entry(lane_id_of(lane));
	close_barrier(lane_id_of(lane));
}

static void close_after_exit(struct lane *lane)
{
	record_exit(lane_id_of(lane));
	close_barrier(lane_id_of(lane));
}

/*
 * REQ-12: the amount is computed once, here, from the entry time; P1 resends
 * reuse it inside services/payment and never ask for it again.
 * @retval -ENOENT the plate no longer has a session.
 * @retval -ERANGE more than 9999 units (from tariff_cents()).
 */
static int freeze_amount(const char *plate, uint32_t *cents)
{
	int64_t entry_ms;

	if (sessions_entry_time(plate, &entry_ms) != 0) {
		return -ENOENT;
	}
	return tariff_cents(k_uptime_get() - entry_ms, cents);
}

/*
 * REQ-05, REQ-12, in the order the specification fixes: plate on the
 * display, amount frozen, amount on the display, then CHARGE. Every way it
 * cannot charge ends in an event, so the table decides what follows: above
 * 9999 units nothing is drawn or charged (EV_TARIFF_OUT_OF_RANGE).
 */
static void start_charge(struct lane *lane)
{
	enum lane_id id = lane_id_of(lane);
	char plate[PLATE_LEN + 1];
	uint32_t cents;

	if (camera_plate(id, plate) != 0) {
		(void)events_post(id, EV_PAYMENT_FAILED);
		return;
	}
	(void)screen_show_plate(plate);

	int err = freeze_amount(plate, &cents);

	if (err != 0) {
		(void)events_post(id, (err == -ERANGE) ? EV_TARIFF_OUT_OF_RANGE
						       : EV_PAYMENT_FAILED);
		return;
	}
	(void)screen_show_amount(cents);
	if (payment_start(plate, cents) != 0) {
		(void)events_post(id, EV_PAYMENT_FAILED);
	}
}

/* REQ-09: motor off first, then red on, green off and one buzzer pulse. */
static void assist(enum lane_id id)
{
	(void)barrier_stop(id);
	camera_abandon(id);
	(void)signals_assist(id);
}

/*
 * The rearm edges (EV_CAR_LEFT, EV_BEAM_OFF) may already be past: a car that
 * left during payment is far before the lane gets here. Then the lane tells
 * itself, so it does not wait for an edge that will never come.
 */
static void rearm_if_car_already_gone(enum lane_id id)
{
	if (presence_car_far(id) && !presence_beam_active(id)) {
		(void)events_post(id, EV_CAR_LEFT);
	}
}

static void request_assistance(struct lane *lane)
{
	assist(lane_id_of(lane));
}

static void request_rearmable_assistance(struct lane *lane)
{
	assist(lane_id_of(lane));
	rearm_if_car_already_gone(lane_id_of(lane));
}

/* Exit: invalidate any charge (P1 CANCEL if one was sent) before
 * requesting assistance; harmless when nothing is being charged.
 */
static void request_exit_assistance(struct lane *lane)
{
	payment_cancel();
	request_assistance(lane);
}

static void request_rearmable_exit_assistance(struct lane *lane)
{
	payment_cancel();
	request_rearmable_assistance(lane);
}

/* ---- Rows shared by both lanes ---- */

/*
 * REQ-01: boot goes to IDLE only with the barrier closed; any other position
 * asks for assistance instead of closing by itself (no automatic homing).
 */
#define ROW_INIT                                                               \
	{                                                                      \
		LATCHING_FAULTS,                                               \
		[EV_BOOT] = GO_IF(barrier_closed, STATE_IDLE, STATE_ASSIST_LOCKED), \
	}

#define ROW_IDLE                                                               \
	{                                                                      \
		LATCHING_FAULTS,                                               \
		[EV_CAR_ARRIVED] = GO_IF(barrier_closed, STATE_IDENTIFYING,    \
					 STATE_ASSIST_LOCKED),                 \
	}

/* REQ-09: rearms when the car is far AND the beam is clear; either may come
 * last, so both edges ask for the other condition.
 */
#define ROW_ASSIST                                                             \
	{                                                                      \
		LATCHING_FAULTS,                                               \
		[EV_CAR_LEFT] = GO_IF(beam_clear, STATE_IDLE, LANE_STATE_NONE), \
		[EV_BEAM_OFF] = GO_IF(car_far, STATE_IDLE, LANE_STATE_NONE),   \
	}

/* REQ-06: the motor stops at the real limit; a missed one is a timeout. */
#define ROW_OPENING                                                            \
	{                                                                      \
		LATCHING_FAULTS,                                               \
		[EV_UPPER_REACHED] = GO(STATE_OPEN),                           \
		[EV_TIMEOUT] = GO(STATE_ASSIST_LOCKED), /* arm stopped halfway */ \
	}

/* REQ-07: the beam must go active, then inactive with the car far. A car
 * that never passes (stationary) keeps green until the passage budget.
 */
#define ROW_OPEN                                                               \
	{                                                                      \
		LATCHING_FAULTS,                                               \
		[EV_BEAM_ON] = GO(STATE_PASSING),                              \
		[EV_TIMEOUT] = GO(STATE_ASSIST_LOCKED), /* barrier left open */ \
	}

/* Beam clear and car far may come in either order: both edges ask. */
#define ROW_PASSING                                                            \
	{                                                                      \
		LATCHING_FAULTS,                                               \
		[EV_BEAM_OFF] = GO_IF(passage_complete, STATE_CLOSING, LANE_STATE_NONE), \
		[EV_CAR_LEFT] = GO_IF(passage_complete, STATE_CLOSING, LANE_STATE_NONE), \
		[EV_TIMEOUT] = GO(STATE_ASSIST_LOCKED), /* beam held or car stayed */ \
	}

/* REQ-08: an obstruction stops the motor (assistance) and never reverses
 * or restarts it, even if the beam clears again.
 */
#define ROW_CLOSING                                                            \
	{                                                                      \
		LATCHING_FAULTS,                                               \
		[EV_LOWER_REACHED] = GO_IF(car_near, STATE_IDENTIFYING, STATE_IDLE), \
		[EV_BEAM_ON] = GO(STATE_ASSIST_LOCKED),                        \
		[EV_TIMEOUT] = GO(STATE_ASSIST_LOCKED), /* arm stopped halfway */ \
	}

/* PASSING keeps OPEN's budget: the beam going active does not restart it.
 * Both lanes move the same barrier model, so they share these budgets.
 */
static const uint16_t barrier_cycle_deadlines_ms[STATE_COUNT] = {
	[STATE_OPENING] = BARRIER_MOTION_BUDGET_MS,
	[STATE_OPEN] = PASSAGE_BUDGET_MS,
	[STATE_PASSING] = LANE_DEADLINE_KEEP,
	[STATE_CLOSING] = BARRIER_MOTION_BUDGET_MS,
};

/* ---- Entry lane rules ---- */

/* STATE_ASSIST_LOCKED has an empty row in both lanes: only a reset leaves it. */
static const struct lane_transition entry_table[STATE_COUNT][EV_COUNT] = {
	[STATE_INIT] = ROW_INIT,
	[STATE_IDLE] = ROW_IDLE,
	[STATE_IDENTIFYING] = {
		LATCHING_FAULTS,
		[EV_PLATE_READ] = GO_IF(plate_may_enter, STATE_OPENING, STATE_ASSIST),
		[EV_CAPTURE_FAILED] = GO(STATE_ASSIST),
		[EV_CAR_LEFT] = GO(STATE_IDLE), /* REQ-03: gave up, no assistance */
	},
	[STATE_OPENING] = ROW_OPENING,
	[STATE_OPEN] = ROW_OPEN,
	[STATE_PASSING] = ROW_PASSING,
	[STATE_CLOSING] = ROW_CLOSING,
	[STATE_ASSIST] = ROW_ASSIST,
};

static const lane_enter_fn entry_actions[STATE_COUNT] = {
	[STATE_IDLE] = wait_for_car,
	[STATE_IDENTIFYING] = start_recognition,
	[STATE_OPENING] = open_barrier,
	[STATE_OPEN] = let_car_through,
	[STATE_CLOSING] = close_after_entry,
	[STATE_ASSIST] = request_rearmable_assistance,
	[STATE_ASSIST_LOCKED] = request_assistance,
};

static const struct lane_def entry_def = {
	.table = &entry_table[0][0],
	.on_enter = entry_actions,
	.deadline_ms = barrier_cycle_deadlines_ms,
	.n_states = STATE_COUNT,
	.n_events = EV_COUNT,
};

/* ---- Exit lane rules ---- */

/* REQ-04: a plate without a session (camera misread, never entered) asks
 * for assistance, never another car's payment.
 */
static const struct lane_transition exit_table[STATE_COUNT][EV_COUNT] = {
	[STATE_INIT] = ROW_INIT,
	[STATE_IDLE] = ROW_IDLE,
	[STATE_IDENTIFYING] = {
		LATCHING_FAULTS,
		[EV_PLATE_READ] = GO_IF(plate_has_session, STATE_CHARGING, STATE_ASSIST),
		[EV_CAPTURE_FAILED] = GO(STATE_ASSIST),
		[EV_CAR_LEFT] = GO(STATE_IDLE), /* REQ-03: gave up, no assistance */
	},
	/* REQ-03/05: a car leaving during payment gives up the charge; the
	 * assistance cancels it (P1 CANCEL) and rearms once the car is gone.
	 */
	[STATE_CHARGING] = {
		LATCHING_FAULTS,
		[EV_PAID] = GO(STATE_OPENING), /* REQ-05: only an exact, current PAID */
		[EV_PAYMENT_FAILED] = GO(STATE_ASSIST),
		[EV_TARIFF_OUT_OF_RANGE] = GO(STATE_ASSIST),
		[EV_CAR_LEFT] = GO(STATE_ASSIST),
	},
	[STATE_OPENING] = ROW_OPENING,
	[STATE_OPEN] = ROW_OPEN,
	[STATE_PASSING] = ROW_PASSING,
	[STATE_CLOSING] = ROW_CLOSING,
	[STATE_ASSIST] = ROW_ASSIST,
};

static const lane_enter_fn exit_actions[STATE_COUNT] = {
	[STATE_IDLE] = wait_for_car,
	[STATE_IDENTIFYING] = start_recognition,
	[STATE_CHARGING] = start_charge,
	[STATE_OPENING] = open_barrier,
	[STATE_OPEN] = let_car_through,
	[STATE_CLOSING] = close_after_exit,
	[STATE_ASSIST] = request_rearmable_exit_assistance,
	[STATE_ASSIST_LOCKED] = request_exit_assistance,
};

static const struct lane_def exit_def = {
	.table = &exit_table[0][0],
	.on_enter = exit_actions,
	.deadline_ms = barrier_cycle_deadlines_ms,
	.n_states = STATE_COUNT,
	.n_events = EV_COUNT,
};

const struct lane_def *states_entry_def(void)
{
	return &entry_def;
}

const struct lane_def *states_exit_def(void)
{
	return &exit_def;
}
