#include <errno.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "parking_hw.h"

struct lane_gpio {
	struct gpio_dt_spec enable;
	struct gpio_dt_spec direction;
	struct gpio_dt_spec red;
	struct gpio_dt_spec green;
	struct gpio_dt_spec buzzer;
	struct gpio_dt_spec upper;
	struct gpio_dt_spec lower;
	struct gpio_dt_spec beam;
	struct gpio_callback limits_cb;
	struct gpio_callback beam_cb;
	atomic_t beam_rose;
	bool ready;
};

#define LANE_GPIO(prefix, name) GPIO_DT_SPEC_GET(DT_ALIAS(prefix##_##name), gpios)
#define LANE_PINS(prefix) { \
	.enable = LANE_GPIO(prefix, motor_enable), \
	.direction = LANE_GPIO(prefix, motor_direction), \
	.red = LANE_GPIO(prefix, light_red), \
	.green = LANE_GPIO(prefix, light_green), \
	.buzzer = LANE_GPIO(prefix, buzzer), \
	.upper = LANE_GPIO(prefix, limit_upper), \
	.lower = LANE_GPIO(prefix, limit_lower), \
	.beam = LANE_GPIO(prefix, beam), \
}

static struct lane_gpio pins[PARKING_LANE_COUNT] = {
	[PARKING_ENTRY] = LANE_PINS(entry),
	[PARKING_EXIT] = LANE_PINS(exit),
};
static struct k_sem *controller_wake;

static void limit_irq(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t mask)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(mask);
	k_sem_give(controller_wake);
}

static void beam_irq(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t mask)
{
	ARG_UNUSED(port);
	ARG_UNUSED(mask);
	struct lane_gpio *lane = CONTAINER_OF(cb, struct lane_gpio, beam_cb);

	atomic_set(&lane->beam_rose, 1);
	k_sem_give(controller_wake);
}

static bool configure(const struct gpio_dt_spec *pin, gpio_flags_t flags)
{
	return gpio_is_ready_dt(pin) && gpio_pin_configure_dt(pin, flags) == 0;
}

static bool setup_interrupts(struct lane_gpio *lane)
{
	/* The board puts both limits on the same GPIO bank, on distinct EXTI lines. */
	if (lane->upper.port != lane->lower.port) {
		return false;
	}
	gpio_init_callback(&lane->limits_cb, limit_irq, BIT(lane->upper.pin) | BIT(lane->lower.pin));
	gpio_init_callback(&lane->beam_cb, beam_irq, BIT(lane->beam.pin));
	return gpio_add_callback(lane->upper.port, &lane->limits_cb) == 0 &&
	       gpio_add_callback(lane->beam.port, &lane->beam_cb) == 0 &&
	       gpio_pin_interrupt_configure_dt(&lane->upper, GPIO_INT_EDGE_BOTH) == 0 &&
	       gpio_pin_interrupt_configure_dt(&lane->lower, GPIO_INT_EDGE_BOTH) == 0 &&
	       gpio_pin_interrupt_configure_dt(&lane->beam, GPIO_INT_EDGE_TO_ACTIVE) == 0;
}

void parking_gpio_init(struct k_sem *wake)
{
	controller_wake = wake;
	for (int i = 0; i < PARKING_LANE_COUNT; i++) {
		struct lane_gpio *lane = &pins[i];
		bool ready = configure(&lane->enable, GPIO_OUTPUT_INACTIVE);

		ready &= configure(&lane->green, GPIO_OUTPUT_INACTIVE);
		ready &= configure(&lane->red, GPIO_OUTPUT_ACTIVE);
		ready &= configure(&lane->direction, GPIO_OUTPUT_INACTIVE);
		ready &= configure(&lane->buzzer, GPIO_OUTPUT_INACTIVE);
		ready &= configure(&lane->upper, GPIO_INPUT);
		ready &= configure(&lane->lower, GPIO_INPUT);
		ready &= configure(&lane->beam, GPIO_INPUT);
		lane->ready = ready && setup_interrupts(lane);
	}
}

void parking_gpio_sample(enum parking_lane_id id, struct parking_inputs *inputs)
{
	struct lane_gpio *lane = &pins[id];

	inputs->beam_rose = atomic_set(&lane->beam_rose, 0) != 0;
	inputs->gpio_fault = !lane->ready;
	if (inputs->gpio_fault) {
		return;
	}
	int upper = gpio_pin_get_dt(&lane->upper);
	int lower = gpio_pin_get_dt(&lane->lower);
	int beam = gpio_pin_get_dt(&lane->beam);

	inputs->gpio_fault = upper < 0 || lower < 0 || beam < 0;
	inputs->upper = upper > 0;
	inputs->lower = lower > 0;
	inputs->beam = beam > 0;
}

static int output(const struct gpio_dt_spec *pin, bool on)
{
	return gpio_is_ready_dt(pin) ? gpio_pin_set_dt(pin, on) : -ENODEV;
}

static int set_motor(enum parking_lane_id id, enum parking_motor command)
{
	const struct lane_gpio *lane = &pins[id];

	if (command == PARKING_STOP) {
		return output(&lane->enable, false);
	}
	if (!lane->ready || (command != PARKING_OPEN && command != PARKING_CLOSE)) {
		return -EINVAL;
	}
	bool opening = command == PARKING_OPEN;
	int upper = gpio_pin_get_dt(&lane->upper);
	int lower = gpio_pin_get_dt(&lane->lower);

	if (upper != !opening || lower != opening) {
		return -EIO;
	}
	int rc = output(&lane->direction, opening);

	if (rc != 0) {
		return rc;
	}
	if (!opening) {
		/* Recheck after direction setup, immediately before enabling the motor. */
		int beam = gpio_pin_get_dt(&lane->beam);

		if (beam != 0) {
			return beam > 0 ? -EAGAIN : beam;
		}
	}
	return output(&lane->enable, true);
}

static int set_signal(enum parking_lane_id id, bool green)
{
	const struct lane_gpio *lane = &pins[id];

	if (green) {
		if (gpio_pin_get_dt(&lane->upper) != 1 || gpio_pin_get_dt(&lane->lower) != 0) {
			return -EIO;
		}
		int rc = output(&lane->red, false);

		return rc == 0 ? output(&lane->green, true) : rc;
	}
	int green_rc = output(&lane->green, false);
	int red_rc = output(&lane->red, true);

	return green_rc != 0 ? green_rc : red_rc;
}

static int set_buzzer(enum parking_lane_id id, bool on)
{
	return output(&pins[id].buzzer, on);
}

const struct parking_io parking_gpio_io = {
	.motor = set_motor,
	.signal = set_signal,
	.buzzer = set_buzzer,
};
