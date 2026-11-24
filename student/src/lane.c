/*
 * lane.c — Deterministic single-lane state machine implementation
 *
 * Implements safe startup, camera recognition, exit tariff freeze & display,
 * payment confirmation, limit-stopped opening/closing within 2500 ms,
 * beam passage verification within 10000 ms, <10 ms obstruction shutdown,
 * buzzer-pulsed assistance, closed-barrier rearm, and latched faults.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "lane.h"
#include "session.h"

#include <errno.h>
#include <string.h>

typedef struct {
	hw_out_pin_t motor_en;
	hw_out_pin_t motor_dir;
	hw_out_pin_t red;
	hw_out_pin_t green;
	hw_out_pin_t buzzer;
	hw_in_pin_t upper;
	hw_in_pin_t lower;
	hw_in_pin_t beam;
} lane_pins_t;

static const lane_pins_t lane_pins[LANE_COUNT] = {
	[LANE_ENTRY] = {
		.motor_en  = HW_OUT_ENTRY_MOTOR_EN,
		.motor_dir = HW_OUT_ENTRY_MOTOR_DIR,
		.red       = HW_OUT_ENTRY_RED,
		.green     = HW_OUT_ENTRY_GREEN,
		.buzzer    = HW_OUT_ENTRY_BUZZER,
		.upper     = HW_IN_ENTRY_UPPER,
		.lower     = HW_IN_ENTRY_LOWER,
		.beam      = HW_IN_ENTRY_BEAM,
	},
	[LANE_EXIT] = {
		.motor_en  = HW_OUT_EXIT_MOTOR_EN,
		.motor_dir = HW_OUT_EXIT_MOTOR_DIR,
		.red       = HW_OUT_EXIT_RED,
		.green     = HW_OUT_EXIT_GREEN,
		.buzzer    = HW_OUT_EXIT_BUZZER,
		.upper     = HW_IN_EXIT_UPPER,
		.lower     = HW_IN_EXIT_LOWER,
		.beam      = HW_IN_EXIT_BEAM,
	},
};

int lane_hw_setup(lane_id_t id)
{
	const lane_pins_t *p = &lane_pins[id];
	const hw_out_pin_t outs[5] = {
		p->motor_en, p->motor_dir, p->red, p->green, p->buzzer
	};
	const hw_in_pin_t ins[3] = { p->upper, p->lower, p->beam };
	hw_uart_id_t cam_uart = (id == LANE_ENTRY) ?
				HW_UART_ENTRY_CAMERA : HW_UART_EXIT_CAMERA;
	int rc;

	for (size_t i = 0U; i < 5U; i++) {
		if (!hw_out_is_ready(outs[i])) {
			return -ENODEV;
		}
		rc = hw_out_configure_inactive(outs[i]);
		if (rc != 0) {
			return rc;
		}
	}

	/* Safe initial state: motor disabled, green off, buzzer off, red on */
	rc = hw_out_set(p->red, true);
	if (rc != 0) {
		return rc;
	}

	for (size_t i = 0U; i < 3U; i++) {
		if (!hw_in_is_ready(ins[i])) {
			return -ENODEV;
		}
		rc = hw_in_configure(ins[i]);
		if (rc != 0) {
			return rc;
		}
		rc = hw_in_register_callback(ins[i]);
		if (rc != 0) {
			return rc;
		}
		rc = hw_in_configure_interrupt(ins[i]);
		if (rc != 0) {
			return rc;
		}
	}

	if (!hw_uart_is_ready(cam_uart)) {
		return -ENODEV;
	}
	if (id == LANE_EXIT && !hw_uart_is_ready(HW_UART_PAYMENT)) {
		return -ENODEV;
	}

	return 0;
}

void lane_init(lane_ctx_t *ctx, lane_id_t id, int64_t now_ms)
{
	(void)memset(ctx, 0, sizeof(*ctx));
	ctx->id = id;
	ctx->state = LANE_STATE_INIT;
	ctx->boot_ms = now_ms;

	camera_init(&ctx->camera, id);
	payment_init(&ctx->payment);
}

static void stop_motor_locked(lane_ctx_t *ctx)
{
	if (hw_out_set(lane_pins[ctx->id].motor_en, false) != 0) {
		ctx->io_fault = true;
	}
	ctx->motor_running = false;
}

static void set_red_only(lane_ctx_t *ctx)
{
	const lane_pins_t *p = &lane_pins[ctx->id];

	if (hw_out_set(p->green, false) != 0 ||
	    hw_out_set(p->red, true) != 0) {
		ctx->io_fault = true;
	}
}

static void set_green_only(lane_ctx_t *ctx)
{
	const lane_pins_t *p = &lane_pins[ctx->id];

	if (hw_out_set(p->red, false) != 0 ||
	    hw_out_set(p->green, true) != 0) {
		ctx->io_fault = true;
	}
}

static void start_buzzer_pulse(lane_ctx_t *ctx, int64_t now_ms)
{
	if (hw_out_set(lane_pins[ctx->id].buzzer, true) != 0) {
		ctx->io_fault = true;
		return;
	}
	ctx->buzzer_active = true;
	ctx->buzzer_end_ms = now_ms + LANE_BUZZER_PULSE_MS;
}

static void service_buzzer(lane_ctx_t *ctx, int64_t now_ms)
{
	if (ctx->buzzer_active && (now_ms >= ctx->buzzer_end_ms)) {
		if (hw_out_set(lane_pins[ctx->id].buzzer, false) != 0) {
			ctx->io_fault = true;
		}
		ctx->buzzer_active = false;
	}
}

void lane_urgent_safety(lane_ctx_t *ctx)
{
	const lane_pins_t *p = &lane_pins[ctx->id];
	unsigned int key = hw_irq_lock();
	int u = hw_in_get(p->upper);
	int l = hw_in_get(p->lower);
	int b = hw_in_get(p->beam);

	if (u < 0 || l < 0 || b < 0) {
		stop_motor_locked(ctx);
		ctx->io_fault = true;
		hw_irq_unlock(key);
		return;
	}

	ctx->upper_active = (u != 0);
	ctx->lower_active = (l != 0);
	ctx->beam_active = (b != 0);

	if (ctx->upper_active && ctx->lower_active) {
		stop_motor_locked(ctx);
		ctx->contradictory_fault = true;
	}

	if (ctx->motor_running) {
		if (ctx->motor_opening) {
			if (ctx->upper_active) {
				stop_motor_locked(ctx);
			}
		} else {
			if (ctx->beam_active) {
				stop_motor_locked(ctx);
				ctx->obstruction_fault = true;
			} else if (ctx->lower_active) {
				stop_motor_locked(ctx);
			}
		}
	}

	if (ctx->state == LANE_STATE_OPEN) {
		if (!ctx->beam_active) {
			ctx->wait_beam_low_first = false;
			if (ctx->passage_beam_seen) {
				ctx->passage_beam_cleared = true;
			}
		} else if (!ctx->wait_beam_low_first) {
			ctx->passage_beam_seen = true;
			ctx->passage_beam_cleared = false;
		}
	}

	hw_irq_unlock(key);
}

static void abort_pending_transactions(lane_ctx_t *ctx)
{
	camera_abort(&ctx->camera);
	if (ctx->id == LANE_EXIT && ctx->payment.state == PAYMENT_STATE_WAITING) {
		payment_cancel_active(&ctx->payment);
	}
	payment_abort_idle(&ctx->payment);
	(void)memset(ctx->plate, 0, sizeof(ctx->plate));
}

void lane_force_latch(lane_ctx_t *ctx, int64_t now_ms)
{
	unsigned int key;

	if (ctx->state == LANE_STATE_LATCHED) {
		return;
	}

	key = hw_irq_lock();
	stop_motor_locked(ctx);
	hw_irq_unlock(key);

	set_red_only(ctx);
	abort_pending_transactions(ctx);

	if (ctx->state != LANE_STATE_ASSISTANCE) {
		start_buzzer_pulse(ctx, now_ms);
	}

	ctx->unrecoverable = true;
	ctx->state = LANE_STATE_LATCHED;
}

static void enter_assistance(lane_ctx_t *ctx, int64_t now_ms, bool unrecoverable)
{
	unsigned int key = hw_irq_lock();

	stop_motor_locked(ctx);
	hw_irq_unlock(key);

	set_red_only(ctx);
	abort_pending_transactions(ctx);
	start_buzzer_pulse(ctx, now_ms);

	ctx->unrecoverable = unrecoverable;
	ctx->state = LANE_STATE_ASSISTANCE;
}

static bool start_motor_open(lane_ctx_t *ctx, int64_t now_ms)
{
	const lane_pins_t *p = &lane_pins[ctx->id];
	unsigned int key = hw_irq_lock();

	if (hw_out_set(p->motor_dir, true) != 0 ||
	    hw_out_set(p->motor_en, true) != 0) {
		stop_motor_locked(ctx);
		ctx->io_fault = true;
		hw_irq_unlock(key);
		return false;
	}

	ctx->motor_opening = true;
	ctx->motor_running = true;
	ctx->motor_start_ms = now_ms;
	ctx->state = LANE_STATE_OPENING;
	hw_irq_unlock(key);
	return true;
}

/*
 * Atomically verifies clear beam, switches signals from green to red,
 * and starts closing so closing can never be enabled into an active beam.
 */
static int transition_open_to_closing(lane_ctx_t *ctx, int64_t now_ms)
{
	const lane_pins_t *p = &lane_pins[ctx->id];
	unsigned int key = hw_irq_lock();
	int b = hw_in_get(p->beam);

	if (b < 0) {
		stop_motor_locked(ctx);
		ctx->io_fault = true;
		hw_irq_unlock(key);
		return -EIO;
	}

	if (b != 0) {
		ctx->beam_active = true;
		ctx->passage_beam_cleared = false;
		hw_irq_unlock(key);
		return -EBUSY;
	}

	if (hw_out_set(p->green, false) != 0 ||
	    hw_out_set(p->red, true) != 0 ||
	    hw_out_set(p->motor_dir, false) != 0 ||
	    hw_out_set(p->motor_en, true) != 0) {
		stop_motor_locked(ctx);
		ctx->io_fault = true;
		hw_irq_unlock(key);
		return -EIO;
	}

	ctx->motor_opening = false;
	ctx->motor_running = true;
	ctx->motor_start_ms = now_ms;
	ctx->state = LANE_STATE_CLOSING;
	hw_irq_unlock(key);
	return 0;
}

static void drain_background_uarts(lane_ctx_t *ctx, int64_t now_ms)
{
	camera_service(&ctx->camera, now_ms);
	if (ctx->id == LANE_EXIT) {
		payment_service(&ctx->payment, now_ms);
	}
}

static void handle_prepare_charge(lane_ctx_t *ctx, const adc_mgr_t *adc,
				  display_ctx_t *disp, int64_t now_ms)
{
	int64_t freeze_ms;
	int32_t amount;

	if (!ctx->lower_active) {
		enter_assistance(ctx, now_ms, true);
		return;
	}

	if (adc_mgr_car_departed(adc, ctx->id, now_ms)) {
		(void)memset(ctx->plate, 0, sizeof(ctx->plate));
		ctx->state = LANE_STATE_IDLE;
		return;
	}

	if (display_has_error(disp)) {
		enter_assistance(ctx, now_ms, false);
		return;
	}

	if (!display_is_ready(disp)) {
		return;
	}

	/* Render plate FIRST, then freeze tariff, then render amount/status & send CHARGE */
	if (display_render_plate(disp, ctx->plate) != 0) {
		enter_assistance(ctx, now_ms, false);
		return;
	}

	freeze_ms = hw_uptime_ms();
	amount = payment_calc_tariff(freeze_ms - ctx->session_entry_ms,
				     CONFIG_PARKING_TARIFF_UNIT_MS);
	if (amount < 0) {
		enter_assistance(ctx, freeze_ms, false);
		return;
	}

	if (display_render_charge_details(disp, (uint32_t)amount) != 0) {
		enter_assistance(ctx, freeze_ms, false);
		return;
	}

	payment_start_charge(&ctx->payment, ctx->plate, (uint32_t)amount,
			     hw_uptime_ms());
	ctx->state = LANE_STATE_PAYING;
}

static void handle_identifying(lane_ctx_t *ctx, const adc_mgr_t *adc,
			       display_ctx_t *disp, int64_t now_ms)
{
	if (!ctx->lower_active) {
		enter_assistance(ctx, now_ms, true);
		return;
	}

	if (adc_mgr_car_departed(adc, ctx->id, now_ms)) {
		camera_abort(&ctx->camera);
		ctx->state = LANE_STATE_IDLE;
		return;
	}

	camera_service(&ctx->camera, now_ms);
	if (ctx->id == LANE_EXIT) {
		payment_service(&ctx->payment, now_ms);
	}

	if (ctx->camera.state == CAMERA_STATE_FAILED) {
		enter_assistance(ctx, now_ms, false);
		return;
	}

	if (ctx->camera.state == CAMERA_STATE_ACCEPTED &&
	    adc_mgr_has_fresh(adc, ctx->id, now_ms)) {
		(void)memcpy(ctx->plate, ctx->camera.plate, CAMERA_PLATE_LEN + 1U);
		camera_abort(&ctx->camera);

		if (ctx->id == LANE_ENTRY) {
			if (session_is_full() || session_has_plate(ctx->plate)) {
				enter_assistance(ctx, now_ms, false);
				return;
			}
			(void)start_motor_open(ctx, now_ms);
		} else {
			const session_t *s = session_find(ctx->plate);

			if (s == NULL) {
				enter_assistance(ctx, now_ms, false);
				return;
			}
			ctx->session_entry_ms = s->entry_time_ms;
			ctx->state = LANE_STATE_PREPARE_CHARGE;
			handle_prepare_charge(ctx, adc, disp, now_ms);
		}
	}
}

static void handle_paying(lane_ctx_t *ctx, const adc_mgr_t *adc, int64_t now_ms)
{
	if (!ctx->lower_active) {
		enter_assistance(ctx, now_ms, true);
		return;
	}

	if (adc_mgr_car_departed(adc, ctx->id, now_ms)) {
		/* Invalidate UUID before sending CANCEL, then request assistance */
		enter_assistance(ctx, now_ms, false);
		return;
	}

	camera_service(&ctx->camera, now_ms);
	payment_service(&ctx->payment, now_ms);

	if (ctx->payment.state == PAYMENT_STATE_PAID &&
	    adc_mgr_has_fresh(adc, ctx->id, now_ms)) {
		payment_abort_idle(&ctx->payment);
		(void)start_motor_open(ctx, now_ms);
		return;
	}

	if (ctx->payment.state == PAYMENT_STATE_FAILED) {
		enter_assistance(ctx, now_ms, false);
	}
}

static void handle_open(lane_ctx_t *ctx, const adc_mgr_t *adc, int64_t now_ms)
{
	drain_background_uarts(ctx, now_ms);

	if (ctx->passage_beam_seen && ctx->passage_beam_cleared &&
	    !ctx->beam_active && adc_mgr_car_departed(adc, ctx->id, now_ms)) {
		int rc = transition_open_to_closing(ctx, now_ms);

		if (rc == 0) {
			if (ctx->id == LANE_ENTRY) {
				(void)session_create(ctx->plate, now_ms);
			} else {
				(void)session_remove(ctx->plate);
			}
			(void)memset(ctx->plate, 0, sizeof(ctx->plate));
		}
		return;
	}

	if ((now_ms - ctx->open_start_ms) >= LANE_PASSAGE_BUDGET_MS) {
		enter_assistance(ctx, now_ms, true);
	}
}

void lane_service(lane_ctx_t *ctx, const adc_mgr_t *adc, display_ctx_t *disp,
		  int64_t now_ms)
{
	lane_urgent_safety(ctx);
	service_buzzer(ctx, now_ms);

	if (ctx->contradictory_fault || ctx->io_fault ||
	    adc_mgr_is_faulted(adc)) {
		lane_force_latch(ctx, now_ms);
	}

	switch (ctx->state) {
	case LANE_STATE_INIT:
		drain_background_uarts(ctx, now_ms);
		if ((now_ms - ctx->boot_ms) >= LANE_INIT_SETTLE_MS) {
			if (ctx->lower_active) {
				set_red_only(ctx);
				ctx->state = LANE_STATE_IDLE;
			} else {
				enter_assistance(ctx, now_ms, true);
			}
		}
		break;

	case LANE_STATE_IDLE:
		drain_background_uarts(ctx, now_ms);
		if (!ctx->lower_active) {
			enter_assistance(ctx, now_ms, true);
			break;
		}
		if (adc_mgr_car_present(adc, ctx->id, now_ms)) {
			camera_start(&ctx->camera, now_ms);
			ctx->state = LANE_STATE_IDENTIFYING;
		}
		break;

	case LANE_STATE_IDENTIFYING:
		handle_identifying(ctx, adc, disp, now_ms);
		break;

	case LANE_STATE_PREPARE_CHARGE:
		drain_background_uarts(ctx, now_ms);
		handle_prepare_charge(ctx, adc, disp, now_ms);
		break;

	case LANE_STATE_PAYING:
		handle_paying(ctx, adc, now_ms);
		break;

	case LANE_STATE_OPENING:
		drain_background_uarts(ctx, now_ms);
		if (ctx->upper_active && !ctx->motor_running) {
			set_green_only(ctx);
			ctx->open_start_ms = now_ms;
			ctx->wait_beam_low_first = ctx->beam_active;
			ctx->passage_beam_seen = false;
			ctx->passage_beam_cleared = false;
			ctx->state = LANE_STATE_OPEN;
		} else if ((now_ms - ctx->motor_start_ms) >= LANE_MOTOR_BUDGET_MS) {
			enter_assistance(ctx, now_ms, true);
		}
		break;

	case LANE_STATE_OPEN:
		handle_open(ctx, adc, now_ms);
		break;

	case LANE_STATE_CLOSING:
		drain_background_uarts(ctx, now_ms);
		if (ctx->obstruction_fault) {
			enter_assistance(ctx, now_ms, true);
		} else if (ctx->lower_active && !ctx->motor_running) {
			ctx->state = LANE_STATE_IDLE;
		} else if ((now_ms - ctx->motor_start_ms) >= LANE_MOTOR_BUDGET_MS) {
			enter_assistance(ctx, now_ms, true);
		}
		break;

	case LANE_STATE_ASSISTANCE:
		drain_background_uarts(ctx, now_ms);
		if (!ctx->unrecoverable && ctx->lower_active &&
		    !ctx->beam_active &&
		    adc_mgr_car_departed(adc, ctx->id, now_ms)) {
			ctx->state = LANE_STATE_IDLE;
		}
		break;

	case LANE_STATE_LATCHED:
		drain_background_uarts(ctx, now_ms);
		break;
	}

	if (ctx->io_fault) {
		lane_force_latch(ctx, now_ms);
	}
}
