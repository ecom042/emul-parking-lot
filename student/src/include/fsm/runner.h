#ifndef PARKING_FSM_RUNNER_H_
#define PARKING_FSM_RUNNER_H_

/*
 * Startup and execution of the lanes: set up each lane's equipment, bind the
 * lane to its rules, then run one thread per lane. A lane whose equipment
 * fails to start is left in assistance and gets no thread, so it can never
 * open; the other lane is not held back by it.
 */

#include "fsm/lane.h"

/**
 * @brief Empty the lot, set up both lanes and the shared distance
 *        acquisition, then queue EV_BOOT for each lane.
 * A lane whose setup fails gets a best-effort assistance (motor stop, red,
 * buzzer pulse) and no EV_BOOT; a distance (ADC4) failure does that to both
 * lanes. Waits 20 ms of virtual time, with the outputs already safe, for the
 * inputs to settle before EV_BOOT. Run once, from main(), before
 * runner_start().
 * @retval 0 every lane is ready.
 * @retval -errno the first setup error (entry lane, exit lane, then
 *         distance); already reported on the console.
 */
int runner_init(void);

/**
 * @brief Start the distance acquisition, then the thread of every lane that
 *        runner_init() made ready, in lane order.
 * A lane's slow setup (the exit display, about 400 ms) runs at the start of
 * its own thread, at its priority, so it never holds the entry lane back; if
 * it fails, that lane is left in assistance and its thread ends. The entry
 * lane has the higher priority.
 */
void runner_start(void);

/**
 * @brief One turn of a lane thread, without blocking for long: apply at most
 *        one queued event (waiting up to 1 ms), apply EV_TIMEOUT if the
 *        state's deadline expired, then service the lane's UARTs (camera,
 *        and the payment terminal on the exit lane).
 * Exposed so that unit tests can drive a lane step by step.
 */
void runner_step(struct lane *lane);

#endif /* PARKING_FSM_RUNNER_H_ */
