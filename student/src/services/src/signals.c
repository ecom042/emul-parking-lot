#include "services/signals.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "hw/indicator.h"

/* REQ-09: assistance gives one buzzer pulse of 100 ms. */
#define BUZZER_PULSE_MS 100

/* Per-lane state: only the work item that ends the buzzer pulse. */
static struct lane_signals {
	struct k_work_delayable buzzer_off; /**< turns the buzzer off after the pulse */
	enum lane_id id;                    /**< lane this entry serves */
} self[LANE_COUNT];

static void end_buzzer_pulse(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	struct lane_signals *lane = CONTAINER_OF(delayable, struct lane_signals, buzzer_off);

	/* A work item has no caller to report to; a stuck buzzer would show in
	 * the buzzer trace of the scenario.
	 */
	(void)indicator_set(&lane_hw_get(lane->id)->buzzer, false);
}

static int init_pins(const struct lane_hw *hw)
{
	const struct gpio_dt_spec *pins[] = {&hw->red, &hw->green, &hw->buzzer};

	for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
		int err = indicator_init(pins[i]);

		if (err != 0) {
			return err;
		}
	}
	return 0;
}

int signals_init(enum lane_id id)
{
	int err = init_pins(lane_hw_get(id));

	if (err != 0) {
		return err;
	}
	self[id].id = id;
	k_work_init_delayable(&self[id].buzzer_off, end_buzzer_pulse);
	return signals_stop(id);
}

int signals_stop(enum lane_id id)
{
	const struct lane_hw *hw = lane_hw_get(id);
	int err = indicator_set(&hw->green, false);

	if (err != 0) {
		return err;
	}
	return indicator_set(&hw->red, true);
}

int signals_go(enum lane_id id)
{
	const struct lane_hw *hw = lane_hw_get(id);
	int err = indicator_set(&hw->red, false);

	if (err != 0) {
		return err;
	}
	return indicator_set(&hw->green, true);
}

int signals_assist(enum lane_id id)
{
	int err = signals_stop(id);

	if (err != 0) {
		return err;
	}
	err = indicator_set(&lane_hw_get(id)->buzzer, true);
	if (err != 0) {
		return err;
	}

	/* With a non-zero delay k_work_reschedule() cannot fail: it returns 1
	 * (scheduled). Rescheduling restarts the 100 ms if a pulse is running.
	 */
	(void)k_work_reschedule(&self[id].buzzer_off, K_MSEC(BUZZER_PULSE_MS));
	return 0;
}
