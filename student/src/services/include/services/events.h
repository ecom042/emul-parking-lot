#ifndef PARKING_SERVICES_EVENTS_H_
#define PARKING_SERVICES_EVENTS_H_

/*
 * Events: facts the services observe and the lane state machines react to.
 *
 * This is the only definition of the event list. Services publish with
 * events_post(); fsm/ uses the same names as columns of its transition
 * tables. The states of a lane are not here: only fsm/ knows them.
 */

#include "hw/lane_hw.h"

/** Facts a lane can be told about. New services add their events here. */
enum lane_event {
	EV_BOOT,            /**< firmware ready: leave the initial state */
	EV_UPPER_REACHED,   /**< opening reached the upper limit; motor already off */
	EV_LOWER_REACHED,   /**< closing reached the lower limit; motor already off */
	EV_LIMITS_CONFLICT, /**< upper and lower limits active at once; motor already off */
	EV_COUNT,
};

/**
 * @brief Queue an event for one lane, without blocking.
 *
 * Safe to call from an interrupt handler, a work item or another thread.
 * Implemented by fsm/, which owns the lanes and their queues.
 *
 * @retval 0 on success.
 * @retval -ENOMSG if the lane's queue is full (the event is lost).
 */
int events_post(enum lane_id lane, enum lane_event event);

#endif /* PARKING_SERVICES_EVENTS_H_ */
