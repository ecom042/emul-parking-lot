/*
 * lane.h — Deterministic single-lane state machine (REQ-01 .. REQ-09)
 *
 * Instantiated twice by controller.c: once for LANE_ENTRY and once for
 * LANE_EXIT. Shares the session table, alternating ADC4 manager and
 * non-blocking ST7789V display controller.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PARKING_LANE_H_
#define PARKING_LANE_H_

#include <stdbool.h>
#include <stdint.h>

#include "adc.h"
#include "camera.h"
#include "display.h"
#include "hw.h"
#include "payment.h"

#ifndef CONFIG_PARKING_TARIFF_UNIT_MS
#define CONFIG_PARKING_TARIFF_UNIT_MS 60000
#endif

#define LANE_INIT_SETTLE_MS    20
#define LANE_MOTOR_BUDGET_MS   2500
#define LANE_PASSAGE_BUDGET_MS 10000
#define LANE_BUZZER_PULSE_MS   100

typedef enum {
	LANE_STATE_INIT = 0,
	LANE_STATE_IDLE,
	LANE_STATE_IDENTIFYING,
	LANE_STATE_PREPARE_CHARGE,
	LANE_STATE_PAYING,
	LANE_STATE_OPENING,
	LANE_STATE_OPEN,
	LANE_STATE_CLOSING,
	LANE_STATE_ASSISTANCE,
	LANE_STATE_LATCHED
} lane_state_t;

typedef struct {
	lane_id_t id;
	lane_state_t state;
	int64_t boot_ms;

	camera_ctx_t camera;
	payment_ctx_t payment;

	bool motor_running;
	bool motor_opening;
	int64_t motor_start_ms;

	bool upper_active;
	bool lower_active;
	bool beam_active;

	bool wait_beam_low_first;
	bool passage_beam_seen;
	bool passage_beam_cleared;
	int64_t open_start_ms;

	char plate[CAMERA_PLATE_LEN + 1U];
	int64_t session_entry_ms;

	bool buzzer_active;
	int64_t buzzer_end_ms;

	bool unrecoverable;
	bool obstruction_fault;
	bool contradictory_fault;
	bool io_fault;
} lane_ctx_t;

int lane_hw_setup(lane_id_t id);
void lane_init(lane_ctx_t *ctx, lane_id_t id, int64_t now_ms);
void lane_urgent_safety(lane_ctx_t *ctx);
void lane_force_latch(lane_ctx_t *ctx, int64_t now_ms);
void lane_service(lane_ctx_t *ctx, const adc_mgr_t *adc, display_ctx_t *disp,
		  int64_t now_ms);

#endif /* PARKING_LANE_H_ */
