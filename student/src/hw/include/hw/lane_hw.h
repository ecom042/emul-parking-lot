#ifndef PARKING_HW_LANE_HW_H_
#define PARKING_HW_LANE_HW_H_

/*
 * The physical equipment of each lane, taken from the board devicetree.
 *
 * This is the only place that knows the devicetree aliases: the rest of the
 * firmware asks for a lane and gets its equipment by name (red, buzzer...).
 */

#include <zephyr/drivers/gpio.h>

/** The two lanes of the parking lot. */
enum lane_id {
	LANE_ENTRY,
	LANE_EXIT,
	LANE_COUNT,
};

/** Equipment of one lane. Motor and sensors join in later steps. */
struct lane_hw {
	struct gpio_dt_spec red;    /**< red light */
	struct gpio_dt_spec green;  /**< green light */
	struct gpio_dt_spec buzzer; /**< buzzer (the model records transitions, no sound) */
};

/**
 * @brief Equipment of one lane.
 *
 * @param id LANE_ENTRY or LANE_EXIT; anything else is a programming error.
 * @return the lane's equipment, never NULL.
 */
const struct lane_hw *lane_hw_get(enum lane_id id);

#endif /* PARKING_HW_LANE_HW_H_ */
