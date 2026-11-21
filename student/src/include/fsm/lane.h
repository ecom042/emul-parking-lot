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

#include <zephyr/kernel.h>

/** State 0 is reserved: in a table cell it means "no transition". */
#define LANE_STATE_NONE 0U

/** Deadline of a state without one. */
#define LANE_DEADLINE_NONE 0U

/**
 * Deadline of a state that keeps the one already running, so a budget can
 * span several states (the 10000 ms passage spans "open" and "passing").
 */
#define LANE_DEADLINE_KEEP UINT16_MAX

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

/** The rules of one lane: transition table, entry actions and deadlines. */
struct lane_def {
	const struct lane_transition *table; /**< n_states * n_events cells, row = state */
	const lane_enter_fn *on_enter;       /**< one per state; NULL entry = nothing to do */
	/**
	 * One per state, in virtual milliseconds, armed when the state is
	 * entered: LANE_DEADLINE_NONE, a budget, or LANE_DEADLINE_KEEP.
	 * NULL = no state has a deadline.
	 */
	const uint16_t *deadline_ms;
	uint8_t n_states;
	uint8_t n_events;
};

/** Runtime of one lane. */
struct lane {
	const struct lane_def *def;
	struct k_msgq *queue;   /**< pending events, one uint8_t each */
	int64_t deadline_at_ms; /**< virtual uptime the deadline expires at */
	bool deadline_armed;    /**< false: no deadline running */
	uint8_t id;             /**< which lane (entry/exit); entry actions use it */
	uint8_t state;          /**< current state */
};

/**
 * Bind a lane to its rules and its event queue and set its initial state.
 *
 * The queue belongs to the caller (K_MSGQ_DEFINE with sizeof(uint8_t)
 * messages), so each lane chooses its own queue size.
 * Runs no entry action and arms no deadline: only entering a state through
 * lane_dispatch() does that.
 */
void lane_init(struct lane *lane, uint8_t id, const struct lane_def *def,
	       struct k_msgq *queue, uint8_t initial);

/** Current state of the lane. */
uint8_t lane_state(const struct lane *lane);

/**
 * Apply one event to the lane.
 *
 * Looks up table[state][event]. With a guard, the lane goes to `next` if the
 * guard says yes and to `otherwise` if it says no. Entering a state (even the
 * same one again) sets that state's deadline, then runs its entry action.
 *
 * @return true if the lane entered a state; false if the event was ignored:
 *         empty cell, guard refused without `otherwise`, or an event or state
 *         outside the table.
 */
bool lane_dispatch(struct lane *lane, uint8_t event);

/**
 * Queue an event for the lane, without blocking.
 *
 * Safe to call from an interrupt handler or from another thread; the event is
 * applied later by lane_process() in the lane's own thread.
 *
 * @return 0 on success; -ENOMSG if the queue is full (the event is lost).
 */
int lane_post(struct lane *lane, uint8_t event);

/**
 * Take one event from the queue, waiting up to `wait`, and dispatch it.
 *
 * @return true if an event was taken (even if the table ignored it);
 *         false if no event arrived within `wait`.
 */
bool lane_process(struct lane *lane, k_timeout_t wait);

/**
 * @brief Whether the current state's deadline has expired.
 * Says true only once: the deadline is disarmed when reported. The caller
 * then applies its timeout event right away (not through the queue), so a
 * timeout is never applied to a state that has already changed.
 * @param now_ms virtual uptime in milliseconds (k_uptime_get()).
 * @return true if a deadline was running and now_ms reached it.
 */
bool lane_deadline_expired(struct lane *lane, int64_t now_ms);

#endif /* PARKING_FSM_LANE_H_ */
