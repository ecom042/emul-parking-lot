/*
 * lane.h — Lane state machine
 *
 * Each lane (entry and exit) runs an independent state machine.
 * Both lanes share the session table and ADC4 peripheral.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LANE_H
#define LANE_H

#include <stdbool.h>
#include <stdint.h>
#include "hw.h"
#include "camera.h"
#include "payment.h"
#include "adc.h"

/* Lane states */
typedef enum {
	LANE_STATE_INIT,         /* Checking initial conditions */
	LANE_STATE_IDLE,         /* Closed, red on, waiting for car */
	LANE_STATE_IDENTIFYING,  /* Car detected, capturing plate */
	LANE_STATE_PAYING,       /* Exit only: waiting for payment */
	LANE_STATE_OPENING,      /* Motor running to open */
	LANE_STATE_OPEN,         /* Barrier open, green on, waiting passage */
	LANE_STATE_CLOSING,      /* Motor running to close */
	LANE_STATE_ASSISTANCE,   /* Fault: stopped, red, buzzer pulse */
	LANE_STATE_LATCHED,      /* Latched fault (contradictory limits, ADC) */
} lane_state_t;

typedef struct {
	lane_id_t id;
	lane_state_t state;

	/* Sub-modules */
	camera_ctx_t camera;
	payment_ctx_t payment;
	adc_ctx_t adc;

	/* Motor state tracking */
	bool motor_running;
	bool motor_direction_open;
	int64_t motor_start_ms;    /* When the motor was started */

	/* Barrier state from limit switches */
	bool limit_upper_active;
	bool limit_lower_active;

	/* Beam state */
	bool beam_active;
	bool beam_was_active;      /* Beam was active during passage */
	bool beam_cleared;         /* Beam went inactive after being active */

	/* Passage tracking */
	int64_t open_start_ms;     /* When the barrier reached full open */

	/* Identification result */
	char recognized_plate[8];
	bool plate_accepted;

	/* Payment result (exit only) */
	uint32_t charge_amount;
	bool charge_displayed;
	bool payment_started;
	int64_t entry_time_ms;     /* Entry time for tariff calculation */

	/* Buzzer timing */
	bool buzzer_pulsing;
	int64_t buzzer_end_ms;

	/* Obstruction tracking */
	bool obstruction_fault;    /* Latched: obstruction during close */
	bool partial_position;     /* Barrier at unknown partial position */

	/* Flags */
	bool initial_check_done;
	bool contradictory_fault;  /* Latched: contradictory limits */
} lane_ctx_t;

/* Initialize a lane. */
void lane_init(lane_ctx_t *ctx, lane_id_t id);

/* Process one iteration of the lane state machine. Called from main loop. */
void lane_process(lane_ctx_t *ctx, int64_t now_ms);

/* EXTI callbacks — called from ISR context, deferred to main loop via flags. */
void lane_on_limit_upper(lane_ctx_t *ctx);
void lane_on_limit_lower(lane_ctx_t *ctx);
void lane_on_beam(lane_ctx_t *ctx);

/* Camera UART receive callback. */
void lane_on_camera_rx(lane_ctx_t *ctx, const uint8_t *data, size_t len);

/* Payment UART receive callback (exit lane only). */
void lane_on_payment_rx(lane_ctx_t *ctx, const uint8_t *data, size_t len);

#endif /* LANE_H */
