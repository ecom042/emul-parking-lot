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

/** Facts a lane can be told about. The comment on each group says who posts it. */
enum lane_event {
	/* main.c */
	EV_BOOT,            /**< firmware ready: leave the initial state */

	/* barrier */
	EV_UPPER_REACHED,   /**< opening reached the upper limit; motor already off */
	EV_LOWER_REACHED,   /**< closing reached the lower limit; motor already off */
	EV_LIMITS_CONFLICT, /**< upper and lower limits active at once; motor already off */

	/* presence */
	EV_BEAM_ON,         /**< something entered the beam */
	EV_BEAM_OFF,        /**< the beam is clear again */
	EV_CAR_ARRIVED,     /**< fresh distance code crossed below 683 */
	EV_CAR_LEFT,        /**< fresh distance code crossed above 1024 */

	/* distance: posted to BOTH lanes */
	EV_ADC_FAULT,       /**< conversion error, invalid sample or no result within 100 ms */

	/* camera */
	EV_PLATE_READ,      /**< valid plate, confidence >= 85, current capture ID */
	EV_CAPTURE_FAILED,  /**< three attempts used up */

	/* payment: exit lane only */
	EV_PAID,            /**< exact PAID for the current UUID and amount */
	EV_PAYMENT_FAILED,  /**< no PAID after 3 sends or CANCELLED; the UUID is already cancelled */

	/* fsm */
	EV_TIMEOUT,             /**< the current state's deadline expired */
	EV_TARIFF_OUT_OF_RANGE, /**< tariff_cents() said -ERANGE (> 9999 units); no charge was sent */

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
