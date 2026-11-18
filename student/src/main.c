#include <errno.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "gate.h"

#define GATE_GPIO(lane, name) GPIO_DT_SPEC_GET(DT_ALIAS(lane##_##name), gpios)
#define GATE_SERVICE_MS 5

struct lane {
	struct gpio_dt_spec outputs[5];
	struct gpio_dt_spec upper;
	struct gpio_dt_spec lower;
	struct gpio_dt_spec beam;
	struct gpio_callback limit_irq;
	struct gpio_callback beam_irq;
	struct k_work_delayable service;
	atomic_t beam_rose;
	atomic_t feedback_ready;
	struct gate gate;
};

#define GATE_LANE(name) {					\
	.outputs = {						\
		[GATE_ENABLE] = GATE_GPIO(name, motor_enable),	\
		[GATE_DIRECTION] = GATE_GPIO(name, motor_direction), \
		[GATE_RED] = GATE_GPIO(name, light_red),		\
		[GATE_GREEN] = GATE_GPIO(name, light_green),	\
		[GATE_BUZZER] = GATE_GPIO(name, buzzer),		\
	},							\
	.upper = GATE_GPIO(name, limit_upper),			\
	.lower = GATE_GPIO(name, limit_lower),			\
	.beam = GATE_GPIO(name, beam),				\
}

static struct lane lanes[] = { GATE_LANE(entry), GATE_LANE(exit) };

static int write_output(void *context, enum gate_output output, bool active)
{
	struct lane *lane = context;

	return gpio_pin_set_dt(&lane->outputs[output], active);
}

static void service_gate(struct k_work *work)
{
	struct lane *lane = CONTAINER_OF(k_work_delayable_from_work(work),
					struct lane, service);
	int64_t now = k_uptime_get();

	if (!atomic_get(&lane->feedback_ready)) {
		gate_step(&lane->gate, (struct gate_inputs) { 0 }, now);
		if (lane->gate.buzzing) {
			k_work_schedule(&lane->service, K_MSEC(GATE_SERVICE_MS));
		}
		return;
	}

	int upper = gpio_pin_get_dt(&lane->upper);
	int lower = gpio_pin_get_dt(&lane->lower);
	int beam = gpio_pin_get_dt(&lane->beam);
	bool beam_rose = atomic_set(&lane->beam_rose, 0) != 0;

	if (upper < 0 || lower < 0 || beam < 0) {
		gate_assist(&lane->gate, now, true);
	}
	gate_step(&lane->gate, (struct gate_inputs) {
		.upper = upper > 0,
		.lower = lower > 0,
		.beam = beam > 0 || (beam_rose && lane->gate.state == GATE_CLOSING),
	}, now);
	k_work_schedule(&lane->service, K_MSEC(GATE_SERVICE_MS));
}

static void limits_changed(const struct device *port, struct gpio_callback *callback,
			   gpio_port_pins_t pins)
{
	struct lane *lane = CONTAINER_OF(callback, struct lane, limit_irq);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	if (atomic_get(&lane->feedback_ready)) {
		k_work_reschedule(&lane->service, K_NO_WAIT);
	}
}

static void beam_blocked(const struct device *port, struct gpio_callback *callback,
			 gpio_port_pins_t pins)
{
	struct lane *lane = CONTAINER_OF(callback, struct lane, beam_irq);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	if (atomic_get(&lane->feedback_ready)) {
		atomic_set(&lane->beam_rose, 1);
		k_work_reschedule(&lane->service, K_NO_WAIT);
	}
}

static int setup_lane(struct lane *lane)
{
	const struct gpio_dt_spec *inputs[] = { &lane->upper, &lane->lower, &lane->beam };
	bool outputs_ready = true;

	lane->gate.write = write_output;
	lane->gate.context = lane;
	for (size_t i = 0; i < ARRAY_SIZE(lane->outputs); ++i) {
		gpio_flags_t initial = i == GATE_RED ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE;

		if (!gpio_is_ready_dt(&lane->outputs[i]) ||
		    gpio_pin_configure_dt(&lane->outputs[i], initial) < 0) {
			outputs_ready = false;
		}
	}
	if (!outputs_ready) {
		return -EIO;
	}

	k_work_init_delayable(&lane->service, service_gate);
	for (size_t i = 0; i < ARRAY_SIZE(inputs); ++i) {
		if (!gpio_is_ready_dt(inputs[i]) || gpio_pin_configure_dt(inputs[i], GPIO_INPUT) < 0) {
			gate_assist(&lane->gate, k_uptime_get(), true);
			k_work_schedule(&lane->service, K_MSEC(GATE_BUZZER_MS));
			return -EIO;
		}
	}

	/* All limit and beam aliases for this board are on the same GPIO port. */
	gpio_init_callback(&lane->limit_irq, limits_changed,
			   BIT(lane->upper.pin) | BIT(lane->lower.pin));
	gpio_init_callback(&lane->beam_irq, beam_blocked, BIT(lane->beam.pin));
	if (gpio_add_callback(lane->upper.port, &lane->limit_irq) < 0 ||
	    gpio_add_callback(lane->beam.port, &lane->beam_irq) < 0 ||
	    gpio_pin_interrupt_configure_dt(&lane->upper, GPIO_INT_EDGE_BOTH) < 0 ||
	    gpio_pin_interrupt_configure_dt(&lane->lower, GPIO_INT_EDGE_BOTH) < 0 ||
	    gpio_pin_interrupt_configure_dt(&lane->beam, GPIO_INT_EDGE_TO_ACTIVE) < 0) {
		gate_assist(&lane->gate, k_uptime_get(), true);
		k_work_schedule(&lane->service, K_MSEC(GATE_BUZZER_MS));
		return -EIO;
	}
	atomic_set(&lane->feedback_ready, 1);
	k_work_schedule(&lane->service, K_NO_WAIT);
	return 0;
}

int main(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(lanes); ++i) {
		if (setup_lane(&lanes[i]) < 0) {
			printk("Parking lane %zu GPIO setup failed\n", i);
		}
	}
	return 0;
}
