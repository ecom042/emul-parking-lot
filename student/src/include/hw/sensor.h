#ifndef PARKING_HW_SENSOR_H_
#define PARKING_HW_SENSOR_H_

/*
 * Sensor: one digital input that interrupts on every change (limit switch or
 * passage beam). No parking rules here: what a change means is decided by
 * the service that owns the sensor.
 */

#include <stdbool.h>

#include <zephyr/drivers/gpio.h>

/**
 * Called on every change of the input, IN INTERRUPT CONTEXT: it must be short
 * and must not block (for example, submit a work item or queue an event).
 *
 * @param ctx    the pointer given to sensor_init().
 * @param active the new level: true when the input is active.
 */
typedef void (*sensor_handler_fn)(void *ctx, bool active);

/** Runtime of one sensor. Fill it with sensor_init(); do not touch the fields. */
struct sensor {
	struct gpio_dt_spec pin;       /**< the input */
	struct gpio_callback callback; /**< registration with the GPIO driver */
	sensor_handler_fn handler;     /**< who is told about each change */
	void *ctx;                     /**< passed back to the handler */
};

/**
 * @brief Configure the pin as an input that interrupts on both edges.
 *
 * @param sensor  runtime to fill; must stay valid while interrupts can fire.
 * @param pin     the input pin (from the devicetree).
 * @param handler called on each change; must not be NULL.
 * @param ctx     passed back to the handler.
 *
 * @retval 0 on success.
 * @retval -ENODEV if the GPIO controller is not ready.
 * @retval -ENOTSUP if the controller cannot interrupt on both edges.
 * @retval -errno other error from the GPIO driver.
 */
int sensor_init(struct sensor *sensor, const struct gpio_dt_spec *pin,
		sensor_handler_fn handler, void *ctx);

/**
 * @brief Current level of the input.
 *
 * @return true if active; false if inactive or if the driver could not read
 *         it (a failed read never reports "active").
 */
bool sensor_is_active(const struct sensor *sensor);

#endif /* PARKING_HW_SENSOR_H_ */
