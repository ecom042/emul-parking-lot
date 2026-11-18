/*
 * controller.c — Top-level two-lane parking lot coordinator implementation
 *
 * Coordinates initialization, deferred EXTI urgent safety checks on both
 * lanes, shared non-blocking ADC4 channel alternation, non-blocking ST7789V
 * display startup, and concurrent entry/exit lane state machines.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "controller.h"
#include "hw.h"
#include "session.h"

#include <string.h>

static controller_t g_ctrl;

void controller_exti_work(void)
{
	lane_urgent_safety(&g_ctrl.lanes[LANE_ENTRY]);
	lane_urgent_safety(&g_ctrl.lanes[LANE_EXIT]);
}

void controller_init(void)
{
	int64_t now_ms = hw_uptime_ms();

	(void)memset(&g_ctrl, 0, sizeof(g_ctrl));
	session_init();

	lane_init(&g_ctrl.lanes[LANE_ENTRY], LANE_ENTRY, now_ms);
	lane_init(&g_ctrl.lanes[LANE_EXIT], LANE_EXIT, now_ms);

	hw_exti_set_work_handler(controller_exti_work);

	if (lane_hw_setup(LANE_ENTRY) != 0) {
		g_ctrl.lanes[LANE_ENTRY].io_fault = true;
	}

	if (lane_hw_setup(LANE_EXIT) != 0) {
		g_ctrl.lanes[LANE_EXIT].io_fault = true;
	}

	(void)adc_mgr_init(&g_ctrl.adc);
	(void)display_start_init(&g_ctrl.display, now_ms);
}

void controller_step(int64_t now_ms)
{
	(void)adc_mgr_service(&g_ctrl.adc, now_ms);
	display_service(&g_ctrl.display, now_ms);

	lane_service(&g_ctrl.lanes[LANE_ENTRY], &g_ctrl.adc,
		     &g_ctrl.display, now_ms);
	lane_service(&g_ctrl.lanes[LANE_EXIT], &g_ctrl.adc,
		     &g_ctrl.display, now_ms);
}

const controller_t *controller_get_state(void)
{
	return &g_ctrl;
}
