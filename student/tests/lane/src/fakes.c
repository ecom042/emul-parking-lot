/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(int, hw_motor_drive, const struct lane_cfg *, bool);
DEFINE_FAKE_VALUE_FUNC(int, hw_motor_stop, const struct lane_cfg *);
DEFINE_FAKE_VALUE_FUNC(bool, hw_motor_running, const struct lane_cfg *);
DEFINE_FAKE_VALUE_FUNC(int, hw_lights_set, const struct lane_cfg *, bool, bool);
DEFINE_FAKE_VALUE_FUNC(int, hw_buzzer_pulse, const struct lane_cfg *);
DEFINE_FAKE_VALUE_FUNC(int, hw_uart_send, enum hw_uart_port, const char *);
DEFINE_FAKE_VALUE_FUNC(int, st7789_show_plate, const char *);
DEFINE_FAKE_VALUE_FUNC(int, st7789_show_charge, uint32_t);

char fake_uart_line[64];
int fake_uart_rc;

static int uart_custom(enum hw_uart_port port, const char *line)
{
	ARG_UNUSED(port);
	strncpy(fake_uart_line, line, sizeof(fake_uart_line) - 1);
	return fake_uart_rc;
}

void fakes_reset(void)
{
	RESET_FAKE(hw_motor_drive);
	RESET_FAKE(hw_motor_stop);
	RESET_FAKE(hw_motor_running);
	RESET_FAKE(hw_lights_set);
	RESET_FAKE(hw_buzzer_pulse);
	RESET_FAKE(hw_uart_send);
	RESET_FAKE(st7789_show_plate);
	RESET_FAKE(st7789_show_charge);
	FFF_RESET_HISTORY();
	memset(fake_uart_line, 0, sizeof(fake_uart_line));
	fake_uart_rc = 0;
	hw_uart_send_fake.custom_fake = uart_custom;
}
