/*
 * Tests of hw/sensor on an emulated GPIO input (see ../app.overlay).
 * gpio_emul_input_set() changes the input level and, like a real EXTI edge,
 * runs the registered callback.
 */
#include <errno.h>
#include <stdbool.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#include "hw/sensor.h"

#define USER_PIN(prop) GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), prop)

static const struct gpio_dt_spec input = USER_PIN(sensor_gpios);
static const struct gpio_dt_spec input_no_edges = USER_PIN(sensor_no_edges_gpios);

static struct sensor sensor;
static int handler_calls;
static bool last_level;
static void *last_ctx;
static int marker; /* its address is the ctx the handler must get back */

static void record_change(void *ctx, bool active)
{
	handler_calls++;
	last_level = active;
	last_ctx = ctx;
}

static void drive_input(const struct gpio_dt_spec *pin, int level)
{
	zassert_ok(gpio_emul_input_set(pin->port, pin->pin, level));
}

static void reset_sensor(void *fixture)
{
	ARG_UNUSED(fixture);
	/* gpio-emul only accepts input levels on a pin configured as input, so
	 * init first. Then put the input back to 0 (the callback may run now)
	 * and only after that clear what the handler recorded.
	 */
	zassert_ok(sensor_init(&sensor, &input, record_change, &marker));
	drive_input(&input, 0);
	handler_calls = 0;
	last_level = false;
	last_ctx = NULL;
}

ZTEST_SUITE(hw_sensor, NULL, NULL, reset_sensor, NULL, NULL);

ZTEST(hw_sensor, test_inactive_input_reads_inactive)
{
	zassert_false(sensor_is_active(&sensor));
	zassert_equal(handler_calls, 0, "no change, no call");
}

ZTEST(hw_sensor, test_rising_edge_reports_active_with_ctx)
{
	drive_input(&input, 1);
	zassert_equal(handler_calls, 1);
	zassert_true(last_level);
	zassert_equal_ptr(last_ctx, &marker);
	zassert_true(sensor_is_active(&sensor));
}

ZTEST(hw_sensor, test_falling_edge_reports_inactive)
{
	drive_input(&input, 1);
	drive_input(&input, 0);
	zassert_equal(handler_calls, 2);
	zassert_false(last_level);
	zassert_false(sensor_is_active(&sensor));
}

ZTEST(hw_sensor, test_init_fails_without_edge_interrupts)
{
	struct sensor no_edges;

	zassert_equal(sensor_init(&no_edges, &input_no_edges, record_change, NULL), -ENOTSUP);
}
