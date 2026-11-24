#include "hw/motor.h"

#include "hw/indicator.h"

/* Both motor pins are plain on/off outputs, so they reuse hw/indicator. */

int motor_init(const struct motor *motor)
{
	/* Enable first: the motor is off before anything else is touched. */
	int err = indicator_init(&motor->enable);

	if (err != 0) {
		return err;
	}
	return indicator_init(&motor->direction);
}

int motor_drive(const struct motor *motor, enum motor_direction direction)
{
	int err = indicator_set(&motor->direction, direction == MOTOR_OPEN);

	if (err != 0) {
		return err;
	}
	return indicator_set(&motor->enable, true);
}

int motor_stop(const struct motor *motor)
{
	return indicator_set(&motor->enable, false);
}
