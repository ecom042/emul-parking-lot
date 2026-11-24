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

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "hw.h"
#include "hw_uart.h"
#include "lane.h"
#include "sessions.h"
#include "st7789.h"
#include "tariff.h"

/** Maior ID de captura aceito pela camera. */
#define CAPTURE_ID_MAX 999999999U

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

/** Estados em que a cancela deve estar parada no fim de curso inferior. */
static bool barrier_should_be_closed(const struct lane *lane)
{
	return lane->state == LANE_IDLE || lane->state == LANE_IDENTIFYING ||
	       lane->state == LANE_DISPLAYING || lane->state == LANE_CHARGING;
}

/** Esquece o ciclo de autorizacao em andamento. */
static void forget_cycle(struct lane *lane)
{
	lane->capture_id = 0;
	lane->tries = 0;
	lane->beam_seen = false;
	lane->beam_cleared = false;
	lane->late_passage = false;
}

/**
 * Pede assistencia: motor off, verde off, vermelho on e um unico pulso de
 * buzzer por pedido. @p to e LANE_ASSIST (rearmavel) ou LANE_LOCKED (ate o
 * reset); LOCKED nunca volta a ASSIST.
 */
static void send_cancel(struct lane *lane);

static void enter_assist(struct lane *lane, enum lane_state to)
{
	bool already = in_assist(lane);

	if (lane->uuid[0] != '\0') {
		/* Cobranca abandonada: invalida o UUID antes do P1 CANCEL. */
		memcpy(lane->cancel_uuid, lane->uuid, sizeof(lane->cancel_uuid));
		lane->uuid[0] = '\0';
		lane->cancel_pending = true;
		send_cancel(lane);
	}

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

/* ---- Acoes ------------------------------------------------------------ */

/** Erro de driver ao comandar a faixa: nao ha como seguir com seguranca. */
static void hw_failed(struct lane *lane)
{
	enter_assist(lane, LANE_LOCKED);
}

/** Proximo ID de captura da faixa: 1..CAPTURE_ID_MAX, nunca 0. */
static uint32_t next_capture_id(struct lane *lane)
{
	lane->id_counter = (lane->id_counter >= CAPTURE_ID_MAX)
				   ? 1U
				   : lane->id_counter + 1U;
	return lane->id_counter;
}

/**
 * Nova tentativa de captura, com ID novo e prazo de CAPTURE_MS contado do
 * envio. Um envio que falha vale como tentativa: sem resposta, vence o prazo.
 */
static void start_capture(struct lane *lane, int64_t now_ms)
{
	char cmd[sizeof("AT+CAPTURE=") + 10];

	lane->tries++;
	lane->capture_id = next_capture_id(lane);
	(void)snprintf(cmd, sizeof(cmd), "AT+CAPTURE=%" PRIu32,
		       lane->capture_id);
	(void)hw_uart_send(lane->cfg->camera, cmd);
	lane->deadline_ms = now_ms + CAPTURE_MS;
	lane->state = LANE_IDENTIFYING;
}

/** Tentativa perdida (confianca baixa ou prazo): outra, ou assistencia. */
static void capture_failed(struct lane *lane, int64_t now_ms)
{
	lane->capture_id = 0;
	if (lane->tries < CAPTURE_TRIES) {
		start_capture(lane, now_ms);
	} else {
		enter_assist(lane, LANE_ASSIST);
	}
}

/**
 * P1 CANCEL do UUID ja invalidado. So um -EBUSY (um envio anterior ainda
 * saindo pela UART) fica para o proximo tick; outro erro nao melhora com
 * novas tentativas.
 */
static void send_cancel(struct lane *lane)
{
	char line[sizeof("P1 CANCEL ") + UUID_LEN];

	(void)snprintf(line, sizeof(line), "P1 CANCEL %s", lane->cancel_uuid);
	if (hw_uart_send(HW_UART_PAYMENT, line) != -EBUSY) {
		lane->cancel_pending = false;
	}
}

/**
 * Um envio da cobranca congelada, sempre com o mesmo UUID e o mesmo valor.
 * Um envio que falha vale como envio: sem resposta, vence o prazo.
 */
static void send_charge(struct lane *lane, int64_t now_ms)
{
	char line[FRAME_MAX];

	lane->tries++;
	(void)snprintf(line, sizeof(line), "P1 CHARGE %s %s %" PRIu32 " BRL",
		       lane->uuid, lane->plate, lane->cents);
	(void)hw_uart_send(HW_UART_PAYMENT, line);
	lane->deadline_ms = now_ms + CHARGE_MS;
}

/**
 * Inicia a cobranca da saida. A ordem do contrato e: placa desenhada ->
 * preco congelado -> valor desenhado -> P1 CHARGE. Aqui so a placa vai para
 * a fila; o resto avanca em DISPLAYING, a cada pedido que o display conclui.
 */
static void start_charge(struct lane *lane, int64_t entry_ms, int64_t now_ms)
{
	if (st7789_show_plate(lane->plate, &lane->disp_ticket) < 0) {
		/* Sem tela nao ha cobranca. */
		enter_assist(lane, LANE_ASSIST);
		return;
	}
	lane->entry_ms = entry_ms;
	lane->price_frozen = false;
	lane->deadline_ms = now_ms + DISPLAY_MS;
	lane->state = LANE_DISPLAYING;
}

/** O pedido esperado ja foi concluido (contadores de 32 bits, com volta). */
static bool display_reached(const struct lane *lane, uint32_t done)
{
	return (int32_t)(done - lane->disp_ticket) >= 0;
}

/** Liga o motor e arma o prazo do movimento. */
static void move_gate(struct lane *lane, bool open, int64_t now_ms)
{
	if (hw_motor_drive(lane->cfg, open) < 0) {
		hw_failed(lane);
		return;
	}
	lane->deadline_ms = now_ms + GATE_MOVE_MS;
	lane->state = open ? LANE_OPENING : LANE_CLOSING;
}

/** Placa aceita: decide pela tabela de sessoes. */
static void plate_accepted(struct lane *lane, const struct at_plate *plate,
			   int64_t now_ms)
{
	lane->capture_id = 0;
	memcpy(lane->plate, plate->plate, sizeof(lane->plate));

	if (!lane->cfg->has_payment) {
		/* Entrada: placa nova e vaga livre. */
		if (sessions_has(lane->plate) || sessions_full()) {
			enter_assist(lane, LANE_ASSIST);
			return;
		}
		move_gate(lane, true, now_ms);
		return;
	}
	/* Saida: so placa com sessao ativa paga e sai. */
	int64_t entry_ms = 0;

	if (sessions_entry_time(lane->plate, &entry_ms) < 0) {
		enter_assist(lane, LANE_ASSIST);
		return;
	}
	start_charge(lane, entry_ms, now_ms);
}

/** Passagem completa: cria a sessao na entrada, encerra na saida. */
static int update_sessions(struct lane *lane, int64_t now_ms)
{
	return lane->cfg->has_payment ? sessions_remove(lane->plate)
				      : sessions_add(lane->plate, now_ms);
}

/**
 * Acompanha a passagem: feixe ativo, depois inativo, e no tick o feixe livre
 * com carro recente a mais de 1500 mm. Feixe que volta zera o "inativo".
 * @return true no tick em que a passagem completa.
 */
static bool passage_completed(struct lane *lane, const struct lane_event *ev)
{
	switch (ev->id) {
	case EV_BEAM_ON:
		lane->beam_seen = true;
		lane->beam_cleared = false;
		return false;
	case EV_BEAM_OFF:
		lane->beam_cleared = lane->beam_seen;
		return false;
	case EV_TICK:
		return lane->beam_cleared && !lane->beam &&
		       car_away(lane, ev->now_ms);
	case EV_LIMITS:
	case EV_DISTANCE:
	case EV_PLATE:
	case EV_PAYMENT:
	case EV_DISPLAY:
	case EV_ADC_FAULT:
	case EV_HW_ERROR:
		return false;
	}
	return false;
}

/** Passagem completa no prazo: so ela mexe na tabela e fecha. */
static void passage_done(struct lane *lane, int64_t now_ms)
{
	if (update_sessions(lane, now_ms) < 0) {
		/* Tabela incoerente com a autorizacao: nao fecha sozinha. */
		enter_assist(lane, LANE_ASSIST);
		return;
	}
	/* Vermelho antes de fechar; o feixe ja esta livre. */
	if (hw_lights_set(lane->cfg, true, false) < 0) {
		hw_failed(lane);
		return;
	}
	lane->beam_seen = false;
	lane->beam_cleared = false;
	move_gate(lane, false, now_ms);
}

/* ---- Estados ---------------------------------------------------------- */

/* Carro chegou com a cancela fechada: comeca a identificacao. */
static void handle_event_idle(struct lane *lane, const struct lane_event *ev)
{
	if (barrier_closed(lane) && lane->presence.present && !lane->served &&
	    code_fresh(lane, ev->now_ms)) {
		/* Uma identificacao por presenca: carro parado nao recomeca. */
		lane->served = true;
		lane->tries = 0;
		start_capture(lane, ev->now_ms);
	}
}

static void handle_event_identifying(struct lane *lane,
				     const struct lane_event *ev)
{
	if ((ev->id == EV_DISTANCE || ev->id == EV_TICK) &&
	    car_away(lane, ev->now_ms)) {
		/* O carro foi embora: abandona e invalida o ID. A amostra chega
		 * antes dos quadros no mesmo tick, entao nenhum +PLATE posterior
		 * autoriza.
		 */
		forget_cycle(lane);
		lane->state = LANE_IDLE;
		return;
	}
	if (ev->id == EV_PLATE) {
		/* So o +PLATE do ID atual conta; os outros sao ignorados. */
		if (ev->plate.id != lane->capture_id) {
			return;
		}
		if (ev->plate.confidence < CONF_MIN) {
			capture_failed(lane, ev->now_ms);
			return;
		}
		plate_accepted(lane, &ev->plate, ev->now_ms);
		return;
	}
	if (ev->id == EV_TICK && ev->now_ms >= lane->deadline_ms) {
		capture_failed(lane, ev->now_ms);
	}
}

/*
 * Mostrando a cobranca: placa desenhada -> congela o preco e pede o valor;
 * valor desenhado -> UUID novo e o primeiro P1 CHARGE. Falha ou prazo do
 * display: assistencia sem cobrar. Carro que sai: abandona (ainda nao ha
 * cobranca a cancelar).
 */
static void handle_event_displaying(struct lane *lane,
				    const struct lane_event *ev)
{
	uint32_t cents = 0;

	if ((ev->id == EV_DISTANCE || ev->id == EV_TICK) &&
	    car_away(lane, ev->now_ms)) {
		forget_cycle(lane);
		lane->state = LANE_IDLE;
		return;
	}
	if (ev->id == EV_TICK) {
		if (ev->now_ms >= lane->deadline_ms) {
			enter_assist(lane, LANE_ASSIST);
		}
		return;
	}
	if (ev->id != EV_DISPLAY || !display_reached(lane, ev->display.done)) {
		return;
	}
	if (ev->display.failed == lane->disp_ticket) {
		enter_assist(lane, LANE_ASSIST);
		return;
	}
	if (!lane->price_frozen) {
		/* Placa na tela: congela o preco agora e pede o valor. */
		if (tariff_cents(ev->now_ms - lane->entry_ms,
				 CONFIG_PARKING_TARIFF_UNIT_MS, &cents) < 0 ||
		    st7789_show_charge(cents, &lane->disp_ticket) < 0) {
			/* Acima de 9999 unidades ou sem tela: nao cobra. */
			enter_assist(lane, LANE_ASSIST);
			return;
		}
		lane->cents = cents;
		lane->price_frozen = true;
		lane->deadline_ms = ev->now_ms + DISPLAY_MS;
		return;
	}
	/* Valor na tela: so agora a cobranca vai ao terminal. */
	uuid_generate(lane->uuid);
	lane->tries = 0;
	lane->state = LANE_CHARGING;
	send_charge(lane, ev->now_ms);
}

/* Cobrando: so o PAID exato da cobranca atual abre. */
static void handle_event_charging(struct lane *lane,
				  const struct lane_event *ev)
{
	if ((ev->id == EV_DISTANCE || ev->id == EV_TICK) &&
	    car_away(lane, ev->now_ms)) {
		/* O carro foi embora: invalida o UUID, CANCEL e assistencia,
		 * antes de qualquer PAID que chegue no mesmo tick.
		 */
		enter_assist(lane, LANE_ASSIST);
		return;
	}
	if (ev->id == EV_PAYMENT) {
		const struct p1_result *r = &ev->payment;

		/* ERROR nao tem UUID; outro UUID e resposta antiga. */
		if (r->status == P1_STATUS_ERROR ||
		    strcmp(r->uuid, lane->uuid) != 0) {
			return;
		}
		if (r->status == P1_STATUS_CANCELLED) {
			enter_assist(lane, LANE_ASSIST);
			return;
		}
		if (r->cents != lane->cents) {
			return; /* valor divergente nunca abre */
		}
		/* Pago: a autorizacao e consumida; duplicata nao reabre. */
		lane->uuid[0] = '\0';
		move_gate(lane, true, ev->now_ms);
		return;
	}
	if (ev->id != EV_TICK) {
		return;
	}
	if (ev->now_ms >= lane->deadline_ms) {
		if (lane->tries < CHARGE_TRIES) {
			send_charge(lane, ev->now_ms);
		} else {
			enter_assist(lane, LANE_ASSIST);
		}
	}
}

/* Abrindo: para no fim de curso superior; verde so com o motor desligado. */
static void handle_event_opening(struct lane *lane, const struct lane_event *ev)
{
	if (ev->id != EV_TICK) {
		return;
	}
	if (lane->upper) {
		/* O work do EXTI ja pode ter desligado; se nao, desliga aqui. */
		if (hw_motor_running(lane->cfg) && hw_motor_stop(lane->cfg) < 0) {
			hw_failed(lane);
			return;
		}
		if (!hw_motor_running(lane->cfg)) {
			if (hw_lights_set(lane->cfg, false, true) < 0) {
				hw_failed(lane);
				return;
			}
			lane->beam_seen = false;
			lane->beam_cleared = false;
			lane->deadline_ms = ev->now_ms + PASS_MS;
			lane->state = LANE_WAIT_PASSAGE;
			return;
		}
	}
	if (ev->now_ms >= lane->deadline_ms) {
		/* Travou ou perdeu o fim de curso: posicao incerta. */
		enter_assist(lane, LANE_LOCKED);
	}
}

/* Aberta: feixe ativo, depois inativo, e carro recente a mais de 1500 mm. */
static void handle_event_wait_passage(struct lane *lane,
				      const struct lane_event *ev)
{
	if (passage_completed(lane, ev)) {
		passage_done(lane, ev->now_ms);
		return;
	}
	if (ev->id == EV_TICK && ev->now_ms >= lane->deadline_ms) {
		/* Cancela aberta e parada: assistencia sem fechar no feixe. O
		 * carro ainda pode completar a passagem depois (late_passage).
		 */
		bool seen = lane->beam_seen;
		bool cleared = lane->beam_cleared;

		enter_assist(lane, LANE_ASSIST);
		lane->beam_seen = seen;
		lane->beam_cleared = cleared;
		lane->late_passage = true;
	}
}

/* Fechando: obstrucao trava (a ISR ja cortou o motor); para no inferior. */
static void handle_event_closing(struct lane *lane, const struct lane_event *ev)
{
	if (ev->id == EV_BEAM_ON) {
		if (lane->lower && !hw_motor_running(lane->cfg)) {
			/* Obstrucao no fechamento pede assistencia, mas a cancela
			 * ja esta no inferior: sem posicao parcial, vale o rearme
			 * da cancela fechada (distancia longe e feixe livre).
			 */
			enter_assist(lane, LANE_ASSIST);
			return;
		}
		/* Parada no meio do curso: sem reverter, travada ate o reset. */
		enter_assist(lane, LANE_LOCKED);
		return;
	}
	if (ev->id != EV_TICK) {
		return;
	}
	if (lane->lower) {
		if (hw_motor_running(lane->cfg) && hw_motor_stop(lane->cfg) < 0) {
			hw_failed(lane);
			return;
		}
		if (!hw_motor_running(lane->cfg)) {
			forget_cycle(lane);
			lane->state = LANE_IDLE;
			return;
		}
	}
	if (ev->now_ms >= lane->deadline_ms) {
		enter_assist(lane, LANE_LOCKED);
	}
}

/*
 * Assistencia rearmavel: so com a cancela fechada, carro longe e feixe livre.
 * Se veio do prazo da passagem, a cancela segue aberta e parada; um carro que
 * completa a passagem depois ainda entra (ou sai) da tabela de sessoes, mas
 * nada fecha sozinho.
 */
static void handle_event_assist(struct lane *lane, const struct lane_event *ev)
{
	if (lane->late_passage) {
		if (passage_completed(lane, ev)) {
			lane->late_passage = false;
			/* Ja em assistencia: um erro aqui nao tem outra reacao. */
			(void)update_sessions(lane, ev->now_ms);
		}
		return;
	}
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
		if (!lane->started) {
			/* Partida: sem fim de curso inferior, pede ajuda e nunca
			 * fecha sozinha.
			 */
			lane->started = true;
			if (!lane->lower || lane->upper) {
				enter_assist(lane, LANE_ASSIST);
				return false;
			}
		}
		if (!lane->lower && barrier_should_be_closed(lane)) {
			/* Perdeu o inferior com a cancela parada fechada: pede
			 * ajuda, sem identificar nem abrir (cobranca cancelada).
			 */
			enter_assist(lane, LANE_ASSIST);
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
	case EV_DISPLAY:
		return true;
	}
	return true;
}

/* ---- API -------------------------------------------------------------- */

void lane_init(struct lane *lane, const struct lane_cfg *cfg)
{
	int rc;

	memset(lane, 0, sizeof(*lane));
	lane->cfg = cfg;
	lane->state = LANE_IDLE;

	/* Saidas seguras ja. A posicao so e classificada no primeiro EV_LIMITS:
	 * no boot o modelo so reafirma o nivel dos sensores na primeira amostra
	 * (ParkingPhysical.cs, Drive), entao uma leitura aqui pode ser velha.
	 */
	rc = hw_motor_stop(cfg);
	if (rc == 0) {
		rc = hw_lights_set(cfg, true, false);
	}
	if (rc < 0) {
		enter_assist(lane, LANE_LOCKED);
	}
}

void lane_handle_event(struct lane *lane, const struct lane_event *ev)
{
	if (ev->id == EV_TICK && lane->cancel_pending) {
		send_cancel(lane);
	}
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
	case LANE_DISPLAYING:
		handle_event_displaying(lane, ev);
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
