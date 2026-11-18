/*
 * controller.h — Top-level two-lane parking lot coordinator
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PARKING_CONTROLLER_H_
#define PARKING_CONTROLLER_H_

#include <stdint.h>

#include "adc.h"
#include "display.h"
#include "lane.h"

typedef struct {
	lane_ctx_t lanes[LANE_COUNT];
	adc_mgr_t adc;
	display_ctx_t display;
} controller_t;

void controller_init(void);
void controller_exti_work(void);
void controller_step(int64_t now_ms);
const controller_t *controller_get_state(void);

#endif /* PARKING_CONTROLLER_H_ */
