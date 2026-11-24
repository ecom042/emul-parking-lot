#ifndef PARKING_SERVICES_PRESENCE_H_
#define PARKING_SERVICES_PRESENCE_H_

/*
 * Presence of a car at a lane: the distance sensor (fresh samples from
 * services/distance on chan_distance) and the passage beam.
 *
 * Distance uses hysteresis (REQ-03): a code below 683 means "arrived", above
 * 1024 means "left", and the band between changes nothing, so noise near a
 * threshold never fakes a new arrival. Events are posted only when the
 * presence changes: EV_CAR_ARRIVED, EV_CAR_LEFT, EV_BEAM_ON, EV_BEAM_OFF.
 * Before the first fresh sample of a lane its presence is unknown: the car is
 * neither near nor far.
 */

#include <stdbool.h>

#include "hw/lane_hw.h"

/**
 * @brief Watch the lane's beam and start reacting to its distance samples.
 * Resets the lane's presence to unknown. Before the lane thread starts.
 * @retval 0 on success.
 * @retval -errno error from the beam sensor setup.
 */
int presence_init(enum lane_id id);

/** @brief Whether the last crossing was below 683 (car at the identification point). */
bool presence_car_near(enum lane_id id);

/** @brief Whether the last crossing was above 1024 (car gone). */
bool presence_car_far(enum lane_id id);

/** @brief Whether something is in the beam right now (read from the pin). */
bool presence_beam_active(enum lane_id id);

#endif /* PARKING_SERVICES_PRESENCE_H_ */
