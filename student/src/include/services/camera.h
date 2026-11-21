#ifndef PARKING_SERVICES_CAMERA_H_
#define PARKING_SERVICES_CAMERA_H_

/*
 * Camera of a lane: one recognition at a time, per lane. A recognition always
 * ends with an event for that lane (EV_PLATE_READ or EV_CAPTURE_FAILED), never
 * by blocking the caller. The lane thread drives the service with
 * camera_poll() on every loop.
 */

#include <stdint.h>

#include "hw/lane_hw.h"
#include "services/wire.h" /* PLATE_LEN */

/**
 * @brief UART setup for the lane's camera. Before the lane thread starts.
 *
 * @retval 0 on success.
 * @retval -ENODEV the camera UART is not ready.
 */
int camera_init(enum lane_id id);

/**
 * @brief Start a recognition: up to 3 attempts, a fresh capture ID each,
 *        1000 ms per attempt (confidence >= 85 accepted).
 *
 * Ends with EV_PLATE_READ or EV_CAPTURE_FAILED for this lane.
 *
 * @retval 0 on success.
 * @retval -EBUSY a recognition is already running on this lane.
 */
int camera_start(enum lane_id id);

/** @brief Stop the current recognition; late replies for its IDs are ignored. */
void camera_abandon(enum lane_id id);

/**
 * @brief Service the camera: read received bytes, assemble lines (128 B max,
 *        1000 ms assembly deadline), check attempt deadlines.
 *
 * Called by the lane thread on every loop (about every 1 ms); never blocks.
 *
 * @param now_ms virtual time.
 */
void camera_poll(enum lane_id id, int64_t now_ms);

/**
 * @brief The plate of the last EV_PLATE_READ.
 *
 * @retval 0 on success, @p plate holds it, NUL-terminated.
 * @retval -ENOENT there is none.
 */
int camera_plate(enum lane_id id, char plate[PLATE_LEN + 1]);

#endif /* PARKING_SERVICES_CAMERA_H_ */
