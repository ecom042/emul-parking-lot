#include "fsm/states.h"

#include "services/barrier.h"
#include "services/events.h"
#include "services/signals.h"

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

/* Guard: closed means lower limit active and upper inactive, so
 * contradictory limits are never "closed".
 */
static bool barrier_closed(const struct lane *lane)
{
	return barrier_is_closed(lane_id_of(lane));
}

/* Entry actions return nothing: a GPIO error has no caller to go back to,
 * and the scenario traces would show a wrong output.
 */
static void show_stop(struct lane *lane)
{
	(void)signals_stop(lane_id_of(lane));
}

/* REQ-09: motor off first, then red on, green off and one buzzer pulse. */
static void request_assistance(struct lane *lane)
{
	(void)barrier_stop(lane_id_of(lane));
	(void)signals_assist(lane_id_of(lane));
}

/*
 * REQ-01: boot goes to IDLE only with the barrier closed; any other position
 * asks for assistance instead of closing by itself (no automatic homing).
 * STATE_ASSIST_LOCKED has an empty row: only a reset leaves it.
 */
static const struct lane_transition entry_table[STATE_COUNT][EV_COUNT] = {
	[STATE_INIT] = {
		LATCHING_FAULTS,
		[EV_BOOT] = GO_IF(barrier_closed, STATE_IDLE, STATE_ASSIST_LOCKED),
	},
	[STATE_IDLE] = {
		LATCHING_FAULTS,
	},
	[STATE_ASSIST] = {
		LATCHING_FAULTS,
	},
};

static const lane_enter_fn entry_actions[STATE_COUNT] = {
	[STATE_IDLE] = show_stop,
	[STATE_ASSIST] = request_assistance,
	[STATE_ASSIST_LOCKED] = request_assistance,
};

static const struct lane_def entry_def = {
	.table = &entry_table[0][0],
	.on_enter = entry_actions,
	.n_states = STATE_COUNT,
	.n_events = EV_COUNT,
};

const struct lane_def *states_entry_def(void)
{
	return &entry_def;
}
