#ifndef PARKING_SERVICES_BARRIER_H_
#define PARKING_SERVICES_BARRIER_H_

/*
 * Barrier of a lane: the motor plus the upper and lower limit switches.
 *
 * The barrier stops its own motor when a limit switch becomes active and only
 * then tells the lane (EV_UPPER_REACHED, EV_LOWER_REACHED or
 * EV_LIMITS_CONFLICT). The motor is stopped in a work item, not in the
 * interrupt, as the controller contract asks. Motion deadlines (2500 ms) are
 * the state machine's job, not the barrier's.
 */

#include <stdbool.h>

#include "hw/lane_hw.h"

/**
 * @brief Stop the motor and start watching both limit switches.
 *
 * Must run once per lane, before the lane can receive barrier events.
 *
 * @retval 0 on success.
 * @retval -errno error from the motor or limit switch setup.
 */
int barrier_init(enum lane_id id);

/**
 * @brief Start opening. The motor stops by itself at the upper limit.
 *
 * @retval 0 on success.
 * @retval -errno error from the GPIO driver.
 */
int barrier_open(enum lane_id id);

/**
 * @brief Start closing. The motor stops by itself at the lower limit.
 *
 * @retval 0 on success.
 * @retval -errno error from the GPIO driver.
 */
int barrier_close(enum lane_id id);

/**
 * @brief Stop the motor wherever the arm is (fault, obstruction, timeout).
 *
 * @retval 0 on success.
 * @retval -errno error from the GPIO driver.
 */
int barrier_stop(enum lane_id id);

/**
 * @brief Whether the barrier is closed: lower limit active, upper inactive.
 */
bool barrier_is_closed(enum lane_id id);

#endif /* PARKING_SERVICES_BARRIER_H_ */
