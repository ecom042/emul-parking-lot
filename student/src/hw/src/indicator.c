#include "hw/indicator.h"

#include <errno.h>

int indicator_init(const struct gpio_dt_spec *pin)
{
	if (!gpio_is_ready_dt(pin)) {
		return -ENODEV;
	}
	return gpio_pin_configure_dt(pin, GPIO_OUTPUT_INACTIVE);
}

int indicator_set(const struct gpio_dt_spec *pin, bool on)
{
	return gpio_pin_set_dt(pin, on);
}
