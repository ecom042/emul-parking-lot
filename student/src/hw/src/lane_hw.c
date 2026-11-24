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
		.camera = DEVICE_DT_GET(DT_ALIAS(entry_camera)),
		.distance_cfg = ADC_CHANNEL_CFG_DT(DT_ALIAS(entry_distance)),
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
		.camera = DEVICE_DT_GET(DT_ALIAS(exit_camera)),
		.distance_cfg = ADC_CHANNEL_CFG_DT(DT_ALIAS(exit_distance)),
	},
};

const struct lane_hw *lane_hw_get(enum lane_id id)
{
	__ASSERT(id < LANE_COUNT, "invalid lane %d", id);
	return &lanes[id];
}

const struct device *payment_terminal_get(void)
{
	return DEVICE_DT_GET(DT_ALIAS(payment_terminal));
}

static const struct st7789 display = ST7789_DT_GET(DT_ALIAS(payment_display));

const struct st7789 *payment_display_get(void)
{
	return &display;
}

/* Both distance channels must sit on the same ADC, or one device is not enough. */
BUILD_ASSERT(DT_SAME_NODE(DT_PARENT(DT_ALIAS(entry_distance)), DT_PARENT(DT_ALIAS(exit_distance))),
	     "entry and exit distance channels must share one ADC");

const struct device *distance_adc_get(void)
{
	return DEVICE_DT_GET(DT_PARENT(DT_ALIAS(entry_distance)));
}
