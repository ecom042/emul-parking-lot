#ifndef PARKING_FSM_LANE_H_
#define PARKING_FSM_LANE_H_

/*
 * Generic engine of a lane state machine.
 *
 * The engine only knows how to apply an event: look up the table cell, check
 * the optional guard, change the state and run the new state's entry action.
 * It knows nothing about barriers, cameras or plates: the rules of each lane
 * (states, events, table, entry actions, guards) are data in a struct
 * lane_def, written elsewhere.
 */

#include <stdbool.h>
#include <stdint.h>

/** State 0 is reserved: in a table cell it means "no transition". */
#define LANE_STATE_NONE 0U

struct lane;

/** Guard: extra condition for a transition. Reads the lane, changes nothing. */
typedef bool (*lane_guard_fn)(const struct lane *lane);

/** Action run when the lane enters a state (outputs, starting a service). */
typedef void (*lane_enter_fn)(struct lane *lane);

/** One table cell: what an event does in one state. */
struct lane_transition {
	uint8_t next;        /**< target state; LANE_STATE_NONE = ignore the event */
	lane_guard_fn guard; /**< NULL = always allowed */
	uint8_t otherwise;   /**< target when the guard says no; NONE = ignore */
};

/** The rules of one lane: transition table and entry actions. */
struct lane_def {
	const struct lane_transition *table; /**< n_states * n_events cells, row = state */
	const lane_enter_fn *on_enter;       /**< one per state; NULL entry = nothing to do */
	uint8_t n_states;
	uint8_t n_events;
};

/** Runtime of one lane. Later steps add the queue, deadlines and services. */
struct lane {
	const struct lane_def *def;
	uint8_t id;    /**< which lane (entry/exit); entry actions use it */
	uint8_t state; /**< current state */
};

/**
 * Bind a lane to its rules and set its initial state.
 *
 * Runs no entry action: only lane_dispatch() changes state and runs actions.
 */
void lane_init(struct lane *lane, uint8_t id, const struct lane_def *def, uint8_t initial);

/** Current state of the lane. */
uint8_t lane_state(const struct lane *lane);

/**
 * Apply one event to the lane.
 *
 * Looks up table[state][event]. With a guard, the lane goes to `next` if the
 * guard says yes and to `otherwise` if it says no. Entering a state (even the
 * same one again) runs that state's entry action, if any.
 *
 * @return true if the lane entered a state; false if the event was ignored:
 *         empty cell, guard refused without `otherwise`, or an event or state
 *         outside the table.
 */
bool lane_dispatch(struct lane *lane, uint8_t event);

#endif /* PARKING_FSM_LANE_H_ */
