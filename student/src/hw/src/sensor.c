#include "hw/sensor.h"

#include <errno.h>

#include <zephyr/sys/__assert.h>
#include <zephyr/sys/util.h>

/* GPIO driver callback (interrupt context): tell the owner the new level. */
static void on_edge(const struct device *port, struct gpio_callback *callback,
		    gpio_port_pins_t pins)
{
	struct sensor *sensor = CONTAINER_OF(callback, struct sensor, callback);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	sensor->handler(sensor->ctx, sensor_is_active(sensor));
}

int sensor_init(struct sensor *sensor, const struct gpio_dt_spec *pin,
		sensor_handler_fn handler, void *ctx)
{
	__ASSERT_NO_MSG(handler != NULL);

	if (!gpio_is_ready_dt(pin)) {
		return -ENODEV;
	}
	sensor->pin = *pin;
	sensor->handler = handler;
	sensor->ctx = ctx;

	int err = gpio_pin_configure_dt(pin, GPIO_INPUT);

	if (err != 0) {
		return err;
	}
	gpio_init_callback(&sensor->callback, on_edge, BIT(pin->pin));
	err = gpio_add_callback_dt(pin, &sensor->callback);
	if (err != 0) {
		return err;
	}
	return gpio_pin_interrupt_configure_dt(pin, GPIO_INT_EDGE_BOTH);
}

bool sensor_is_active(const struct sensor *sensor)
{
	return gpio_pin_get_dt(&sensor->pin) > 0;
}
