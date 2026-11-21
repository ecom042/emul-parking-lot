#ifndef PARKING_HW_LANE_HW_H_
#define PARKING_HW_LANE_HW_H_

/*
 * The physical equipment of each lane, taken from the board devicetree.
 *
 * This is the only place that knows the devicetree aliases: the rest of the
 * firmware asks for a lane and gets its equipment by name (red, buzzer...).
 */

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>

#include "hw/motor.h"

/** The two lanes of the parking lot. */
enum lane_id {
	LANE_ENTRY,
	LANE_EXIT,
	LANE_COUNT,
};

/** Equipment of one lane. */
struct lane_hw {
	struct motor motor;              /**< barrier motor */
	struct gpio_dt_spec limit_upper; /**< active exactly at 90° (open) */
	struct gpio_dt_spec limit_lower; /**< active exactly at 0° (closed) */
	struct gpio_dt_spec beam;        /**< active while something crosses the lane */
	struct gpio_dt_spec red;    /**< red light */
	struct gpio_dt_spec green;  /**< green light */
	struct gpio_dt_spec buzzer; /**< buzzer (the model records transitions, no sound) */
	const struct device *camera;         /**< camera UART: USART3 (entry) / USART2 (exit) */
	struct adc_channel_cfg distance_cfg; /**< ADC4 channel 17 (entry) / 18 (exit) */
};

/**
 * @brief Equipment of one lane.
 *
 * @param id LANE_ENTRY or LANE_EXIT; anything else is a programming error.
 * @return the lane's equipment, never NULL.
 */
const struct lane_hw *lane_hw_get(enum lane_id id);

/**
 * @brief UART of the payment terminal (LPUART1).
 *
 * Exit lane only, so it is not part of struct lane_hw.
 *
 * @return the device, never NULL (readiness is checked by uart_line_init()).
 */
const struct device *payment_terminal_get(void);

#endif /* PARKING_HW_LANE_HW_H_ */
