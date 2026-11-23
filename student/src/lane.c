/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file lane.c
 * @brief Maquina de estados de uma faixa (entrada ou saida).
 *
 * Cada evento passa primeiro por lane_handle_event(), que guarda as entradas
 * (fins de curso, feixe, distancia) e trata o que vale em qualquer estado:
 * fins de curso contraditorios, falha do ADC e erro de driver travam em
 * LOCKED. Depois o evento vai para o tratador do estado atual.
 */

#include <string.h>

#include <zephyr/sys/util.h>

#include "hw.h"
#include "lane.h"

/* ---- Auxiliares ------------------------------------------------------- */

static bool in_assist(const struct lane *lane)
{
	return lane->state == LANE_ASSIST || lane->state == LANE_LOCKED;
}

/** Amostra de distancia com menos de ADC_FRESH_MS. */
static bool code_fresh(const struct lane *lane, int64_t now_ms)
{
	return lane->have_code && (now_ms - lane->code_ms) < ADC_FRESH_MS;
}

/** Distancia recente acima de ADC_LEAVE (cerca de 1500 mm). */
static bool car_away(const struct lane *lane, int64_t now_ms)
{
	return code_fresh(lane, now_ms) && lane->code > ADC_LEAVE;
}

/** Cancela fechada e parada: so inferior, motor desligado. */
static bool barrier_closed(const struct lane *lane)
{
	return lane->lower && !lane->upper && !hw_motor_running(lane->cfg);
}

/** Esquece o ciclo de autorizacao em andamento. */
static void forget_cycle(struct lane *lane)
{
	lane->capture_id = 0;
	lane->tries = 0;
	lane->beam_seen = false;
	lane->beam_cleared = false;
}

/**
 * Pede assistencia: motor off, verde off, vermelho on e um unico pulso de
 * buzzer por pedido. @p to e LANE_ASSIST (rearmavel) ou LANE_LOCKED (ate o
 * reset); LOCKED nunca volta a ASSIST.
 */
static void enter_assist(struct lane *lane, enum lane_state to)
{
	bool already = in_assist(lane);

	/* Ja estamos no estado mais seguro possivel: um erro aqui nao tem
	 * outra saida, so seguir travado.
	 */
	(void)hw_motor_stop(lane->cfg);
	(void)hw_lights_set(lane->cfg, true, false);
	if (!already) {
		(void)hw_buzzer_pulse(lane->cfg);
	}
	forget_cycle(lane);
	lane->deadline_ms = 0;
	if (lane->state != LANE_LOCKED) {
		lane->state = to;
	}
}

/* ---- Estados ---------------------------------------------------------- */

static void handle_event_idle(struct lane *lane, const struct lane_event *ev)
{
	ARG_UNUSED(lane);
	ARG_UNUSED(ev);
}

static void handle_event_identifying(struct lane *lane,
				     const struct lane_event *ev)
{
	ARG_UNUSED(lane);
	ARG_UNUSED(ev);
}

static void handle_event_charging(struct lane *lane,
				  const struct lane_event *ev)
{
	ARG_UNUSED(lane);
	ARG_UNUSED(ev);
}

static void handle_event_opening(struct lane *lane, const struct lane_event *ev)
{
	ARG_UNUSED(lane);
	ARG_UNUSED(ev);
}

static void handle_event_wait_passage(struct lane *lane,
				      const struct lane_event *ev)
{
	ARG_UNUSED(lane);
	ARG_UNUSED(ev);
}

static void handle_event_wait_beam_clear(struct lane *lane,
					 const struct lane_event *ev)
{
	ARG_UNUSED(lane);
	ARG_UNUSED(ev);
}

static void handle_event_closing(struct lane *lane, const struct lane_event *ev)
{
	ARG_UNUSED(lane);
	ARG_UNUSED(ev);
}

/* Assistencia rearmavel: so com a cancela fechada, carro longe e feixe livre. */
static void handle_event_assist(struct lane *lane, const struct lane_event *ev)
{
	if (barrier_closed(lane) && car_away(lane, ev->now_ms) && !lane->beam) {
		lane->state = LANE_IDLE;
	}
}

/* Travada ate o reset: nada sai daqui. O buzzer termina sozinho (hw_io.c). */
static void handle_event_locked(struct lane *lane, const struct lane_event *ev)
{
	ARG_UNUSED(lane);
	ARG_UNUSED(ev);
}

/* ---- Entradas comuns a todos os estados ------------------------------- */

/** Guarda as entradas do evento. @return false se o evento ja foi tratado. */
static bool update_inputs(struct lane *lane, const struct lane_event *ev)
{
	enum presence_change change;

	switch (ev->id) {
	case EV_LIMITS:
		lane->upper = ev->limits.upper;
		lane->lower = ev->limits.lower;
		if (lane->upper && lane->lower) {
			/* Contraditorio: trava mesmo que a falha suma depois. */
			enter_assist(lane, LANE_LOCKED);
			return false;
		}
		return true;
	case EV_DISTANCE:
		lane->code = ev->code;
		lane->code_ms = ev->now_ms;
		lane->have_code = true;
		change = presence_update(&lane->presence, ev->code);
		if (change == PRESENCE_LEFT) {
			/* O proximo carro sera identificado de novo. */
			lane->served = false;
		}
		return true;
	case EV_BEAM_ON:
		lane->beam = true;
		return true;
	case EV_BEAM_OFF:
		lane->beam = false;
		return true;
	case EV_ADC_FAULT:
	case EV_HW_ERROR:
		enter_assist(lane, LANE_LOCKED);
		return false;
	case EV_TICK:
	case EV_PLATE:
	case EV_PAYMENT:
		return true;
	}
	return true;
}

/* ---- API -------------------------------------------------------------- */

void lane_init(struct lane *lane, const struct lane_cfg *cfg)
{
	bool upper = false;
	bool lower = false;
	bool beam = false;
	int rc;

	memset(lane, 0, sizeof(*lane));
	lane->cfg = cfg;
	lane->state = LANE_IDLE;

	rc = hw_motor_stop(cfg);
	if (rc == 0) {
		rc = hw_lights_set(cfg, true, false);
	}
	if (rc == 0) {
		rc = hw_limits_read(cfg, &upper, &lower);
	}
	if (rc == 0) {
		rc = hw_beam_read(cfg, &beam);
	}
	lane->upper = upper;
	lane->lower = lower;
	lane->beam = beam;

	if (rc < 0 || (upper && lower)) {
		enter_assist(lane, LANE_LOCKED);
	} else if (!lower || upper) {
		/* Sem fim de curso inferior: pede ajuda, nunca fecha sozinha. */
		enter_assist(lane, LANE_ASSIST);
	}
}

void lane_handle_event(struct lane *lane, const struct lane_event *ev)
{
	if (!update_inputs(lane, ev)) {
		return;
	}

	switch (lane->state) {
	case LANE_IDLE:
		handle_event_idle(lane, ev);
		break;
	case LANE_IDENTIFYING:
		handle_event_identifying(lane, ev);
		break;
	case LANE_CHARGING:
		handle_event_charging(lane, ev);
		break;
	case LANE_OPENING:
		handle_event_opening(lane, ev);
		break;
	case LANE_WAIT_PASSAGE:
		handle_event_wait_passage(lane, ev);
		break;
	case LANE_WAIT_BEAM_CLEAR:
		handle_event_wait_beam_clear(lane, ev);
		break;
	case LANE_CLOSING:
		handle_event_closing(lane, ev);
		break;
	case LANE_ASSIST:
		handle_event_assist(lane, ev);
		break;
	case LANE_LOCKED:
		handle_event_locked(lane, ev);
		break;
	}
}
