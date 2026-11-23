/* SPDX-License-Identifier: Apache-2.0 */
#include "fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(int, hw_motor_drive, const struct lane_cfg *, bool);
DEFINE_FAKE_VALUE_FUNC(int, hw_motor_stop, const struct lane_cfg *);
DEFINE_FAKE_VALUE_FUNC(bool, hw_motor_running, const struct lane_cfg *);
DEFINE_FAKE_VALUE_FUNC(int, hw_lights_set, const struct lane_cfg *, bool, bool);
DEFINE_FAKE_VALUE_FUNC(int, hw_buzzer_pulse, const struct lane_cfg *);
DEFINE_FAKE_VALUE_FUNC(int, hw_beam_read, const struct lane_cfg *, bool *);
DEFINE_FAKE_VALUE_FUNC(int, hw_limits_read, const struct lane_cfg *, bool *, bool *);
DEFINE_FAKE_VALUE_FUNC(int, hw_uart_send, enum hw_uart_port, const char *);
DEFINE_FAKE_VALUE_FUNC(int, st7789_show_plate, const char *);
DEFINE_FAKE_VALUE_FUNC(int, st7789_show_charge, uint32_t);

bool fake_upper, fake_lower, fake_beam;

static int limits_custom(const struct lane_cfg *cfg, bool *upper, bool *lower)
{
	ARG_UNUSED(cfg);
	*upper = fake_upper;
	*lower = fake_lower;
	return 0;
}

static int beam_custom(const struct lane_cfg *cfg, bool *active)
{
	ARG_UNUSED(cfg);
	*active = fake_beam;
	return 0;
}

void fakes_reset(void)
{
	RESET_FAKE(hw_motor_drive);
	RESET_FAKE(hw_motor_stop);
	RESET_FAKE(hw_motor_running);
	RESET_FAKE(hw_lights_set);
	RESET_FAKE(hw_buzzer_pulse);
	RESET_FAKE(hw_beam_read);
	RESET_FAKE(hw_limits_read);
	RESET_FAKE(hw_uart_send);
	RESET_FAKE(st7789_show_plate);
	RESET_FAKE(st7789_show_charge);
	FFF_RESET_HISTORY();
	fake_upper = false;
	fake_lower = true;
	fake_beam = false;
	hw_limits_read_fake.custom_fake = limits_custom;
	hw_beam_read_fake.custom_fake = beam_custom;
}
