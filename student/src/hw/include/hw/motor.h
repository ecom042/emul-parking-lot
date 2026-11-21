#ifndef PARKING_HW_MOTOR_H_
#define PARKING_HW_MOTOR_H_

/*
 * Barrier motor: an enable output (the motor turns while it is on) and a
 * direction output (on = open/up, off = close/down). No parking rules here:
 * when to start and stop is decided by services/barrier.
 */

#include <zephyr/drivers/gpio.h>

/** The two outputs of one barrier motor. */
struct motor {
	struct gpio_dt_spec enable;    /**< on: the motor turns */
	struct gpio_dt_spec direction; /**< on: opens; off: closes */
};

/** Direction of travel of the barrier arm. */
enum motor_direction {
	MOTOR_CLOSE,
	MOTOR_OPEN,
};

/**
 * @brief Configure both outputs off: motor stopped, direction "close".
 *
 * @retval 0 on success.
 * @retval -ENODEV if the GPIO controller is not ready.
 * @retval -errno other error from the GPIO driver.
 */
int motor_init(const struct motor *motor);

/**
 * @brief Turn the motor in one direction.
 *
 * Sets the direction before the enable, so the motor never starts in the
 * previous direction.
 *
 * @retval 0 on success.
 * @retval -errno error from the GPIO driver.
 */
int motor_drive(const struct motor *motor, enum motor_direction direction);

/**
 * @brief Stop the motor (enable off; the direction output is left as is).
 *
 * @retval 0 on success.
 * @retval -errno error from the GPIO driver.
 */
int motor_stop(const struct motor *motor);

#endif /* PARKING_HW_MOTOR_H_ */
