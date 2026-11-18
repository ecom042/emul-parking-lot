/*
 * main.c — Firmware entry point for parking_lot_u5
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "controller.h"
#include "hw.h"

int main(void)
{
	controller_init();

	for (;;) {
		controller_step(hw_uptime_ms());
		hw_wait_tick();
	}
}
