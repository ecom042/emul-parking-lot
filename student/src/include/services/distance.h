#ifndef PARKING_SERVICES_DISTANCE_H_
#define PARKING_SERVICES_DISTANCE_H_

/*
 * Distance sensors of both lanes, read through the shared ADC4. This service
 * only acquires and publishes samples. The thresholds that decide "a car
 * arrived" (683) or "a car left" (1024) are parking rules and live in
 * services/presence.
 */

#include <stdint.h>

#include <zephyr/zbus/zbus.h>

#include "hw/lane_hw.h"

/** One fresh ADC4 sample of one lane. */
struct distance_sample {
	enum lane_id lane; /**< which channel: 17 = entry, 18 = exit */
	uint16_t code;     /**< raw 12-bit code: 0 = 0 mm, 4095 = 6000 mm or more */
	int64_t taken_ms;  /**< virtual time of completion */
};

ZBUS_CHAN_DECLARE(chan_distance); /**< publishes struct distance_sample */

/**
 * @brief Start the periodic acquisition.
 *
 * One conversion at a time, alternating channels, completion checked without
 * blocking and observed within 100 ms. Any fault posts EV_ADC_FAULT to both
 * lanes and stops the acquisition (latched until reset).
 *
 * @retval 0 on success.
 * @retval -ENODEV the ADC is not ready.
 * @retval -errno error from adc4_init().
 */
int distance_init(void);

/**
 * @brief Schedule the periodic poll on the system work queue.
 *
 * Call after distance_init(). Polling runs every few milliseconds with the
 * virtual uptime and stops by itself once a fault is latched.
 */
void distance_start(void);

/**
 * @brief Advance the acquisition one step, without blocking.
 *
 * Normally called by the work item behind distance_start(). Exposed so that
 * unit tests can drive the clock.
 *
 * @param now_ms virtual time in milliseconds.
 */
void distance_poll(int64_t now_ms);

#endif /* PARKING_SERVICES_DISTANCE_H_ */
