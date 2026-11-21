#ifndef PARKING_FSM_STATES_H_
#define PARKING_FSM_STATES_H_

/*
 * The rules of the lanes: states, transition table and entry actions.
 * fsm/lane.c is the engine that applies them; this module only describes
 * them, as data plus small actions that call services/.
 */

#include "fsm/lane.h"

/** States of a lane. 0 is LANE_STATE_NONE ("no transition") in a table cell. */
enum lane_state {
	STATE_INIT = 1,      /**< booting: outputs safe, waiting for EV_BOOT */
	STATE_IDLE,          /**< barrier closed, red on, waiting for a car */
	STATE_IDENTIFYING,   /**< camera recognition running (up to 3 attempts) */
	STATE_OPENING,       /**< plate accepted, motor opening (2500 ms budget) */
	STATE_ASSIST,        /**< assistance at a closed barrier: rearms when the car leaves */
	STATE_ASSIST_LOCKED, /**< assistance until reset: position, limits or ADC fault */
	STATE_COUNT,
};

/**
 * @brief Rules of the entry lane.
 * Start the lane in STATE_INIT and post EV_BOOT when the firmware is ready.
 * @return the rules, never NULL; they live for the whole run.
 */
const struct lane_def *states_entry_def(void);

#endif /* PARKING_FSM_STATES_H_ */
