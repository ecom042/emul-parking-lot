#ifndef PARKING_SERVICES_SIGNALS_H_
#define PARKING_SERVICES_SIGNALS_H_

/*
 * Signals of a lane: red light, green light and buzzer as three modes the
 * driver understands (stop, go, assistance).
 *
 * This service does not decide when green is allowed: the state machine only
 * calls signals_go() after the barrier reached the upper limit with the motor
 * off (REQ-06).
 */

#include "hw/lane_hw.h"

/**
 * @brief Configure the lane's red, green and buzzer pins, then show stop.
 *
 * Must run once per lane before any other signals_* call.
 *
 * @retval 0 on success.
 * @retval -errno error from the GPIO driver.
 */
int signals_init(enum lane_id id);

/**
 * @brief Stop: green off, red on.
 *
 * Green goes off before red goes on, so there is never a moment with the lane
 * showing only green while it stops.
 *
 * @retval 0 on success.
 * @retval -errno error from the GPIO driver.
 */
int signals_stop(enum lane_id id);

/**
 * @brief Go: red off, green on.
 *
 * @retval 0 on success.
 * @retval -errno error from the GPIO driver.
 */
int signals_go(enum lane_id id);

/**
 * @brief Assistance: stop, plus one 100 ms buzzer pulse (REQ-09).
 *
 * Returns right away; a delayed work item turns the buzzer off later, so the
 * caller never blocks.
 *
 * @retval 0 on success.
 * @retval -errno error from the GPIO driver.
 */
int signals_assist(enum lane_id id);

#endif /* PARKING_SERVICES_SIGNALS_H_ */
