/*
 * lane.c — Lane state machine implementation
 *
 * Core controller logic for entry and exit lanes. Each lane runs
 * independently with its own camera, barrier, and signals, sharing
 * the session table and ADC4 peripheral.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lane.h"
#include "session.h"
#include "display.h"
#include <string.h>
#include <zephyr/kernel.h>

#define MOTOR_TIMEOUT_MS    2500
#define PASSAGE_TIMEOUT_MS  10000
#define BUZZER_PULSE_MS     100
#define OBSTRUCTION_MAX_MS  10

/* ── Forward declarations ──────────────────────────────────────── */

static void lane_enter_assistance(lane_ctx_t *ctx);
static void lane_enter_latched(lane_ctx_t *ctx);
static void lane_stop_motor(lane_ctx_t *ctx);
static void lane_start_motor_open(lane_ctx_t *ctx);
static void lane_start_motor_close(lane_ctx_t *ctx);

/* ── Initialization ──────────────────────────────────────────────── */

void lane_init(lane_ctx_t *ctx, lane_id_t id)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->id = id;
	ctx->state = LANE_STATE_INIT;
	ctx->motor_running = false;
	ctx->motor_direction_open = false;
	ctx->limit_upper_active = false;
	ctx->limit_lower_active = false;
	ctx->beam_active = false;

	camera_init(&ctx->camera, id);
	payment_init(&ctx->payment);
	adc_ctx_init(&ctx->adc, id);
}

/* ── EXTI callbacks ──────────────────────────────────────────────── */

void lane_on_limit_upper(lane_ctx_t *ctx)
{
	int val = hw_read_limit_upper(ctx->id);
	if (val < 0) {
		return; /* LCOV_EXCL_LINE: read error unreachable */
	}
	ctx->limit_upper_active = (val != 0);

	/* If motor is running to open and upper limit reached, stop motor */
	if (ctx->limit_upper_active && ctx->motor_running &&
	    ctx->motor_direction_open) {
		lane_stop_motor(ctx);
	}
}

void lane_on_limit_lower(lane_ctx_t *ctx)
{
	int val = hw_read_limit_lower(ctx->id);
	if (val < 0) {
		return; /* LCOV_EXCL_LINE: read error unreachable */
	}
	ctx->limit_lower_active = (val != 0);

	/* If motor is running to close and lower limit reached, stop motor */
	if (ctx->limit_lower_active && ctx->motor_running &&
	    !ctx->motor_direction_open) {
		lane_stop_motor(ctx);
	}
}

void lane_on_beam(lane_ctx_t *ctx)
{
	int val = hw_read_beam(ctx->id);
	if (val < 0) {
		return; /* LCOV_EXCL_LINE: read error unreachable */
	}
	bool was = ctx->beam_active;
	ctx->beam_active = (val != 0);

	if (ctx->beam_active) {
		ctx->beam_was_active = true;

		/* Obstruction during closing: stop motor immediately */
		if (ctx->state == LANE_STATE_CLOSING && ctx->motor_running) {
			lane_stop_motor(ctx);
			ctx->obstruction_fault = true;
		}
	}

	if (was && !ctx->beam_active) {
		/* Beam was active and now clear */
		if (ctx->beam_was_active) {
			ctx->beam_cleared = true;
		}
	}
}

/* Camera UART receive */
void lane_on_camera_rx(lane_ctx_t *ctx, const uint8_t *data, size_t len)
{
	camera_rx_feed(&ctx->camera, data, len);
}

/* Payment UART receive */
void lane_on_payment_rx(lane_ctx_t *ctx, const uint8_t *data, size_t len)
{
	payment_rx_feed(&ctx->payment, data, len);
}

/* ── Motor control ───────────────────────────────────────────────── */

static void lane_stop_motor(lane_ctx_t *ctx)
{
	hw_motor_enable(ctx->id, false);
	ctx->motor_running = false;
}

static void lane_start_motor_open(lane_ctx_t *ctx)
{
	hw_motor_direction(ctx->id, true);
	hw_motor_enable(ctx->id, true);
	ctx->motor_running = true;
	ctx->motor_direction_open = true;
	ctx->motor_start_ms = k_uptime_get();
}

static void lane_start_motor_close(lane_ctx_t *ctx)
{
	hw_motor_direction(ctx->id, false);
	hw_motor_enable(ctx->id, true);
	ctx->motor_running = true;
	ctx->motor_direction_open = false;
	ctx->motor_start_ms = k_uptime_get();
}

/* ── Signals ─────────────────────────────────────────────────────── */

static void lane_set_red(lane_ctx_t *ctx)
{
	hw_light_red(ctx->id, true);
	hw_light_green(ctx->id, false);
}

static void lane_set_green(lane_ctx_t *ctx)
{
	hw_light_red(ctx->id, false);
	hw_light_green(ctx->id, true);
}

static void lane_buzzer_pulse(lane_ctx_t *ctx)
{
	hw_buzzer(ctx->id, true);
	ctx->buzzer_pulsing = true;
	ctx->buzzer_end_ms = k_uptime_get() + BUZZER_PULSE_MS;
}

/* ── Assistance ──────────────────────────────────────────────────── */

static void lane_enter_assistance(lane_ctx_t *ctx)
{
	lane_stop_motor(ctx);
	lane_set_red(ctx);
	lane_buzzer_pulse(ctx);

	/* Cancel any pending payment (exit lane only) */
	if (ctx->id == LANE_EXIT &&
	    ctx->payment.state == PAYMENT_STATE_WAIT_RESULT) {
		payment_cancel(&ctx->payment);
	}

	/* Clear identification state */
	ctx->plate_accepted = false;
	memset(ctx->recognized_plate, 0, sizeof(ctx->recognized_plate));
	ctx->charge_displayed = false;
	ctx->payment_started = false;

	ctx->state = LANE_STATE_ASSISTANCE;
}

static void lane_enter_latched(lane_ctx_t *ctx)
{
	lane_stop_motor(ctx);
	lane_set_red(ctx);
	lane_buzzer_pulse(ctx);
	ctx->state = LANE_STATE_LATCHED;
}

/* ── ADC management ──────────────────────────────────────────────── */

static void lane_adc_cycle(lane_ctx_t *ctx, int64_t now_ms)
{
	if (ctx->adc.fault) {
		if (ctx->state != LANE_STATE_LATCHED) {
			lane_enter_latched(ctx);
		}
		return;
	}

	if (ctx->adc.pending) {
		int rc = adc_poll(&ctx->adc, now_ms);
		if (rc == -EAGAIN) {
			return; /* Still pending */
		}
		if (rc != 0) {
			/* ADC fault */
			lane_enter_latched(ctx);
			return;
		}
		/* Completed. Submit next read immediately. */
		adc_submit(&ctx->adc);
	} else {
		/* No pending read — submit one */
		int rc = adc_submit(&ctx->adc);
		if (rc != 0 && rc != -EBUSY) {
			lane_enter_latched(ctx);
		}
	}
}

/* ── Contradictory limits check ──────────────────────────────────── */

static bool lane_check_contradictory(lane_ctx_t *ctx)
{
	if (ctx->limit_upper_active && ctx->limit_lower_active) {
		ctx->contradictory_fault = true;
		lane_enter_latched(ctx);
		return true;
	}
	return false;
}

/* ── Buzzer processing ───────────────────────────────────────────── */

static void lane_process_buzzer(lane_ctx_t *ctx, int64_t now_ms)
{
	if (ctx->buzzer_pulsing && now_ms >= ctx->buzzer_end_ms) {
		hw_buzzer(ctx->id, false);
		ctx->buzzer_pulsing = false;
	}
}

/* ── State handlers ──────────────────────────────────────────────── */

static void lane_state_init(lane_ctx_t *ctx, int64_t now_ms)
{
	/* Read initial limit switch states */
	int upper = hw_read_limit_upper(ctx->id);
	int lower = hw_read_limit_lower(ctx->id);

	if (upper < 0 || lower < 0) {
		lane_enter_latched(ctx); /* LCOV_EXCL_LINE: read error */
		return;
	}

	ctx->limit_upper_active = (upper != 0);
	ctx->limit_lower_active = (lower != 0);

	/* Contradictory limits: both active */
	if (ctx->limit_upper_active && ctx->limit_lower_active) {
		ctx->contradictory_fault = true;
		lane_enter_latched(ctx);
		return;
	}

	/* Partial position: neither upper nor lower — could be mid-travel
	 * (partial startup). Request assistance. */
	if (!ctx->limit_upper_active && !ctx->limit_lower_active) {
		lane_enter_assistance(ctx);
		return;
	}

	/* Upper limit active without lower: partial open — assistance */
	if (ctx->limit_upper_active && !ctx->limit_lower_active) {
		lane_enter_assistance(ctx);
		return;
	}

	/* Lower limit active: barrier is properly closed */
	if (ctx->limit_lower_active && !ctx->limit_upper_active) {
		ctx->state = LANE_STATE_IDLE;
		ctx->initial_check_done = true;
		lane_set_red(ctx);
		return;
	}
}

static void lane_state_idle(lane_ctx_t *ctx, int64_t now_ms)
{
	if (lane_check_contradictory(ctx)) {
		return;
	}

	/* Need a fresh ADC sample with car present */
	if (ctx->adc.valid && adc_is_car_present(&ctx->adc)) {
		/* Start camera capture */
		ctx->state = LANE_STATE_IDENTIFYING;
		camera_reset(&ctx->camera);
		camera_start_capture(&ctx->camera);
	}
}

static void lane_state_identifying(lane_ctx_t *ctx, int64_t now_ms)
{
	if (lane_check_contradictory(ctx)) {
		return;
	}

	/* Check if car departed (abandon) */
	if (ctx->adc.valid && adc_is_car_departed(&ctx->adc)) {
		camera_reset(&ctx->camera);
		ctx->plate_accepted = false;
		ctx->state = LANE_STATE_IDLE;
		return;
	}

	/* Process camera protocol */
	bool done = camera_process(&ctx->camera, now_ms);
	if (!done) {
		return;
	}

	if (ctx->camera.state == CAMERA_STATE_FAILED) {
		/* All attempts exhausted: assistance */
		lane_enter_assistance(ctx);
		return;
	}

	if (ctx->camera.state == CAMERA_STATE_DONE) {
		/* Plate recognized */
		strncpy(ctx->recognized_plate, ctx->camera.plate,
			CAMERA_PLATE_LEN);
		ctx->recognized_plate[CAMERA_PLATE_LEN] = '\0';
		ctx->plate_accepted = true;

		if (ctx->id == LANE_ENTRY) {
			/* Entry: check for available spot */
			if (session_is_full()) {
				lane_enter_assistance(ctx);
				return;
			}
			if (session_has_plate(ctx->recognized_plate)) {
				lane_enter_assistance(ctx);
				return;
			}
			/* Authorized: open barrier */
			lane_start_motor_open(ctx);
			ctx->state = LANE_STATE_OPENING;
		} else {
			/* Exit: check for active session */
			session_t *s = session_find(ctx->recognized_plate);
			if (s == NULL) {
				lane_enter_assistance(ctx);
				return;
			}
			ctx->entry_time_ms = s->entry_time_ms;

			/* Calculate tariff and display */
			int64_t elapsed = now_ms - ctx->entry_time_ms;
			int32_t amount = payment_calc_tariff(elapsed,
				CONFIG_PARKING_TARIFF_UNIT_MS);
			if (amount < 0) {
				/* Overflow (>9999 units) */
				lane_enter_assistance(ctx);
				return;
			}
			ctx->charge_amount = (uint32_t)amount;

			/* Display payment screen (plate first, then amount) */
			display_payment_screen(ctx->recognized_plate,
					       ctx->charge_amount);
			ctx->charge_displayed = true;

			/* Send payment charge */
			payment_reset(&ctx->payment);
			payment_start_charge(&ctx->payment,
					     ctx->recognized_plate,
					     ctx->charge_amount);
			ctx->payment_started = true;

			ctx->state = LANE_STATE_PAYING;
		}
	}
}

static void lane_state_paying(lane_ctx_t *ctx, int64_t now_ms)
{
	if (lane_check_contradictory(ctx)) {
		return;
	}

	/* Check if car departed (abandon) */
	if (ctx->adc.valid && adc_is_car_departed(&ctx->adc)) {
		/* Invalidate UUID and cancel before assistance */
		if (ctx->payment.state == PAYMENT_STATE_WAIT_RESULT) {
			payment_cancel(&ctx->payment);
		}
		ctx->payment_started = false;
		ctx->plate_accepted = false;
		ctx->charge_displayed = false;
		lane_enter_assistance(ctx);
		return;
	}

	/* Process payment protocol */
	bool done = payment_process(&ctx->payment, now_ms);

	if (ctx->payment.state == PAYMENT_STATE_PAID) {
		/* Payment confirmed: open barrier */
		lane_start_motor_open(ctx);
		ctx->state = LANE_STATE_OPENING;
		return;
	}

	if (done && ctx->payment.state == PAYMENT_STATE_FAILED) {
		/* Payment failed: assistance */
		lane_enter_assistance(ctx);
		return;
	}
}

static void lane_state_opening(lane_ctx_t *ctx, int64_t now_ms)
{
	if (lane_check_contradictory(ctx)) {
		return;
	}

	/* Motor timeout */
	if (ctx->motor_running &&
	    (now_ms - ctx->motor_start_ms) >= MOTOR_TIMEOUT_MS) {
		ctx->partial_position = true;
		lane_enter_assistance(ctx);
		return;
	}

	/* Upper limit reached and motor stopped: fully open */
	if (ctx->limit_upper_active && !ctx->motor_running) {
		/* Green on */
		lane_set_green(ctx);

		ctx->beam_was_active = false;
		ctx->beam_cleared = false;
		ctx->open_start_ms = now_ms;
		ctx->state = LANE_STATE_OPEN;
	}
}

static void lane_state_open(lane_ctx_t *ctx, int64_t now_ms)
{
	if (lane_check_contradictory(ctx)) {
		return;
	}

	/* Passage timeout */
	if ((now_ms - ctx->open_start_ms) >= PASSAGE_TIMEOUT_MS) {
		lane_enter_assistance(ctx);
		return;
	}

	/* Wait for beam active then inactive with car departed */
	if (ctx->beam_was_active && ctx->beam_cleared &&
	    !ctx->beam_active && ctx->adc.valid &&
	    adc_is_car_departed(&ctx->adc)) {
		/* Passage complete! */
		if (ctx->id == LANE_ENTRY) {
			/* Create session */
			session_create(ctx->recognized_plate, now_ms);
		} else {
			/* Remove session (exit) */
			session_remove(ctx->recognized_plate);
		}

		/* Prepare to close */
		lane_set_red(ctx);

		ctx->plate_accepted = false;
		memset(ctx->recognized_plate, 0,
		       sizeof(ctx->recognized_plate));
		ctx->charge_displayed = false;
		ctx->payment_started = false;

		/* Start closing */
		lane_start_motor_close(ctx);
		ctx->state = LANE_STATE_CLOSING;
	}
}

static void lane_state_closing(lane_ctx_t *ctx, int64_t now_ms)
{
	if (lane_check_contradictory(ctx)) {
		return;
	}

	/* Obstruction fault (set by beam ISR) */
	if (ctx->obstruction_fault) {
		/* Motor was already stopped by ISR. Enter assistance.
		 * This is a partial-position fault — no auto recovery. */
		ctx->partial_position = true;
		lane_enter_assistance(ctx);
		return;
	}

	/* Motor timeout */
	if (ctx->motor_running &&
	    (now_ms - ctx->motor_start_ms) >= MOTOR_TIMEOUT_MS) {
		ctx->partial_position = true;
		lane_enter_assistance(ctx);
		return;
	}

	/* Lower limit reached and motor stopped: fully closed */
	if (ctx->limit_lower_active && !ctx->motor_running) {
		ctx->state = LANE_STATE_IDLE;
	}
}

static void lane_state_assistance(lane_ctx_t *ctx, int64_t now_ms)
{
	if (lane_check_contradictory(ctx)) {
		return;
	}

	/* Partial-position faults require intervention/reset */
	if (ctx->partial_position || ctx->obstruction_fault) {
		return;
	}

	/* Rearm conditions: car departed and beam clear, barrier closed */
	if (ctx->limit_lower_active && !ctx->beam_active &&
	    ctx->adc.valid && adc_is_car_departed(&ctx->adc)) {
		ctx->state = LANE_STATE_IDLE;
		camera_reset(&ctx->camera);
		payment_reset(&ctx->payment);
	}
}

static void lane_state_latched(lane_ctx_t *ctx, int64_t now_ms)
{
	/* Latched faults stay until reset. Only process buzzer. */
	(void)now_ms;
}

/* ── Main process ────────────────────────────────────────────────── */

void lane_process(lane_ctx_t *ctx, int64_t now_ms)
{
	/* Always process buzzer timing */
	lane_process_buzzer(ctx, now_ms);

	/* Always run ADC cycle (unless latched) */
	if (ctx->state != LANE_STATE_LATCHED) {
		lane_adc_cycle(ctx, now_ms);
	}

	/* Always check contradictory limits */
	if (ctx->state != LANE_STATE_LATCHED &&
	    ctx->state != LANE_STATE_INIT) {
		if (lane_check_contradictory(ctx)) {
			return;
		}
	}

	switch (ctx->state) {
	case LANE_STATE_INIT:
		lane_state_init(ctx, now_ms);
		break;
	case LANE_STATE_IDLE:
		lane_state_idle(ctx, now_ms);
		break;
	case LANE_STATE_IDENTIFYING:
		lane_state_identifying(ctx, now_ms);
		break;
	case LANE_STATE_PAYING:
		lane_state_paying(ctx, now_ms);
		break;
	case LANE_STATE_OPENING:
		lane_state_opening(ctx, now_ms);
		break;
	case LANE_STATE_OPEN:
		lane_state_open(ctx, now_ms);
		break;
	case LANE_STATE_CLOSING:
		lane_state_closing(ctx, now_ms);
		break;
	case LANE_STATE_ASSISTANCE:
		lane_state_assistance(ctx, now_ms);
		break;
	case LANE_STATE_LATCHED:
		lane_state_latched(ctx, now_ms);
		break;
	}
}
