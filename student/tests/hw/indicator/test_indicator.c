/*
 * Tests of hw/indicator on an emulated GPIO pin (zephyr,gpio-emul, see
 * ../app.overlay). gpio_emul_output_get() reads back what the real GPIO
 * driver API wrote to the pin.
 */
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

#include "hw/indicator.h"

static const struct gpio_dt_spec pin =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), indicator_gpios);

static int pin_output(void)
{
	return gpio_emul_output_get(pin.port, pin.pin);
}

ZTEST_SUITE(hw_indicator, NULL, NULL, NULL, NULL, NULL);

ZTEST(hw_indicator, test_init_then_set_drives_the_pin)
{
	zassert_ok(indicator_init(&pin));
	zassert_equal(pin_output(), 0, "starts off");

	zassert_ok(indicator_set(&pin, true));
	zassert_equal(pin_output(), 1);

	zassert_ok(indicator_set(&pin, false));
	zassert_equal(pin_output(), 0);
}
