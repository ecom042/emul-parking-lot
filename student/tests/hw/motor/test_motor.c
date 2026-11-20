/*
 * Tests of hw/motor on two emulated GPIO pins (see ../app.overlay).
 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#include "hw/motor.h"

#define USER_PIN(prop) GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), prop)

static const struct motor motor = {
	.enable = USER_PIN(motor_enable_gpios),
	.direction = USER_PIN(motor_direction_gpios),
};

static int output_of(const struct gpio_dt_spec *pin)
{
	return gpio_emul_output_get(pin->port, pin->pin);
}

static void init_motor(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_ok(motor_init(&motor));
}

ZTEST_SUITE(hw_motor, NULL, NULL, init_motor, NULL, NULL);

ZTEST(hw_motor, test_init_leaves_motor_stopped_towards_close)
{
	zassert_equal(output_of(&motor.enable), 0, "motor off");
	zassert_equal(output_of(&motor.direction), 0, "direction close");
}

ZTEST(hw_motor, test_drive_open_sets_direction_and_enable)
{
	zassert_ok(motor_drive(&motor, MOTOR_OPEN));
	zassert_equal(output_of(&motor.direction), 1);
	zassert_equal(output_of(&motor.enable), 1);
}

ZTEST(hw_motor, test_drive_close_sets_direction_and_enable)
{
	zassert_ok(motor_drive(&motor, MOTOR_OPEN));
	zassert_ok(motor_drive(&motor, MOTOR_CLOSE));
	zassert_equal(output_of(&motor.direction), 0);
	zassert_equal(output_of(&motor.enable), 1);
}

ZTEST(hw_motor, test_stop_turns_enable_off_and_keeps_direction)
{
	zassert_ok(motor_drive(&motor, MOTOR_OPEN));
	zassert_ok(motor_stop(&motor));
	zassert_equal(output_of(&motor.enable), 0);
	zassert_equal(output_of(&motor.direction), 1);
}
