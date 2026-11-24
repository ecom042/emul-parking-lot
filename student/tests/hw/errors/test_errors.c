/*
 * Error paths of hw/ that the emulators never produce:
 *
 * - "device not ready": a test-only device whose init fails, so the kernel
 *   marks it not ready. Each module must refuse it with -ENODEV before
 *   touching the driver.
 * - programming errors: a NULL sensor handler trips the module's __ASSERT.
 *   CONFIG_ZTEST_ASSERT_HOOK turns an expected assert into the end of the
 *   test thread instead of a halt, so reaching the end of the test body
 *   would mean the assert did not fire.
 */
#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

#include "hw/adc4.h"
#include "hw/indicator.h"
#include "hw/sensor.h"
#include "hw/uart_line.h"

static int fail_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return -EIO;
}

DEVICE_DEFINE(broken_device, "broken", fail_init, NULL, NULL, NULL, POST_KERNEL,
	      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

static const struct device *const broken = DEVICE_GET(broken_device);

/* A pin on the broken device, as the devicetree would describe it. */
static const struct gpio_dt_spec broken_pin = {.port = DEVICE_GET(broken_device), .pin = 0};

static void on_change(void *ctx, bool active)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(active);
}

ZTEST_SUITE(hw_errors, NULL, NULL, NULL, NULL, NULL);

ZTEST(hw_errors, test_the_broken_device_is_not_ready)
{
	zassert_false(device_is_ready(broken), "precondition of every test below");
}

ZTEST(hw_errors, test_indicator_refuses_a_controller_not_ready)
{
	zassert_equal(indicator_init(&broken_pin), -ENODEV);
}

ZTEST(hw_errors, test_sensor_refuses_a_controller_not_ready)
{
	struct sensor sensor;

	zassert_equal(sensor_init(&sensor, &broken_pin, on_change, NULL), -ENODEV);
}

ZTEST(hw_errors, test_uart_line_refuses_a_uart_not_ready)
{
	zassert_equal(uart_line_init(broken), -ENODEV);
}

ZTEST(hw_errors, test_adc4_refuses_an_adc_not_ready)
{
	const struct adc_channel_cfg cfg = {.channel_id = 1};

	zassert_equal(adc4_init(broken, &cfg, 1), -ENODEV);
}

ZTEST(hw_errors, test_sensor_without_handler_is_a_programming_error)
{
	struct sensor sensor;

	ztest_set_assert_valid(true);
	(void)sensor_init(&sensor, &broken_pin, NULL, NULL);

	ztest_test_fail(); /* not reached when the assert fires */
}
