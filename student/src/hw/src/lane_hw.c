#include "hw/lane_hw.h"

#include <zephyr/sys/__assert.h>

#define LANE_GPIO(alias) GPIO_DT_SPEC_GET(DT_ALIAS(alias), gpios)

static const struct lane_hw lanes[LANE_COUNT] = {
	[LANE_ENTRY] = {
		.motor = {
			.enable = LANE_GPIO(entry_motor_enable),
			.direction = LANE_GPIO(entry_motor_direction),
		},
		.limit_upper = LANE_GPIO(entry_limit_upper),
		.limit_lower = LANE_GPIO(entry_limit_lower),
		.beam = LANE_GPIO(entry_beam),
		.red = LANE_GPIO(entry_light_red),
		.green = LANE_GPIO(entry_light_green),
		.buzzer = LANE_GPIO(entry_buzzer),
	},
	[LANE_EXIT] = {
		.motor = {
			.enable = LANE_GPIO(exit_motor_enable),
			.direction = LANE_GPIO(exit_motor_direction),
		},
		.limit_upper = LANE_GPIO(exit_limit_upper),
		.limit_lower = LANE_GPIO(exit_limit_lower),
		.beam = LANE_GPIO(exit_beam),
		.red = LANE_GPIO(exit_light_red),
		.green = LANE_GPIO(exit_light_green),
		.buzzer = LANE_GPIO(exit_buzzer),
	},
};

const struct lane_hw *lane_hw_get(enum lane_id id)
{
	__ASSERT(id < LANE_COUNT, "invalid lane %d", id);
	return &lanes[id];
}
