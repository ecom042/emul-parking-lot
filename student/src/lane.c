/*
 * Máquina de estados de uma faixa. Veja o diagrama em lane.h.
 *
 * Regras de segurança que valem em qualquer estado (REQ-01, REQ-06, REQ-08):
 *  - fins de curso superior e inferior juntos -> assistência travada;
 *  - o motor só para pelo fim de curso real (nunca pelo batente) ou por
 *    prazo/obstrução;
 *  - o verde só acende depois do superior, com o motor JÁ desligado;
 *  - antes de fechar: verde apagado, vermelho aceso, feixe livre.
 */
#include "lane.h"

#include <string.h>

#include <zephyr/sys/printk.h>

#include "hw.h"
#include "protocol.h"
#include "sessions.h"
#include "tariff.h"
#include "ui.h"

/* Contadores compartilhados pelas duas faixas (zerados no reset). */
static uint32_t next_capture_id;
static uint32_t payment_sequence;

static const char *const lane_names[LANE_COUNT] = {"entry", "exit"};

void lane_module_reset(void)
{
	next_capture_id = 1U;
	payment_sequence = 0U;
}

const char *lane_state_name(enum lane_state s)
{
	static const char *const names[] = {
		"STARTUP", "IDLE", "IDENTIFYING", "SHOW_PLATE", "SHOW_AMOUNT",
		"PAYING",  "OPENING", "OPEN", "CLOSING", "ASSIST",
	};

	return names[s];
}

/** Troca de estado com uma linha no console (diagnóstico; não conta na nota). */
static void enter(struct lane *l, enum lane_state s, int64_t now_ms)
{
	printk("[%lld] %s: %s -> %s\n", (long long)now_ms, lane_names[l->id],
	       lane_state_name(l->state), lane_state_name(s));
	l->state = s;
}

/** A cancela está fechada de forma coerente (inferior aceso, superior apagado)? */
static bool closed_ok(const struct lane *l)
{
	return hw_limit_lower(l->id) && !hw_limit_upper(l->id);
}

/* ---- Pagamento ---------------------------------------------------------- */

/**
 * Desiste da cobrança atual (REQ-05): PRIMEIRO invalida o UUID (nenhuma
 * resposta posterior abre a cancela), DEPOIS manda P1 CANCEL.
 */
static void cancel_payment(struct lane *l)
{
	char frame[UART_FRAME_MAX];
	int n;

	l->uuid_active = false;
	n = payment_format_cancel(frame, sizeof(frame), l->uuid);
	if (n > 0) {
		(void)hw_uart_send(UART_PAYMENT, frame, (size_t)n);
	}
}

/** Envia (ou reenvia) a MESMA cobrança: mesmo UUID, mesmo valor. */
static void send_charge(struct lane *l, int64_t now_ms)
{
	char frame[UART_FRAME_MAX];
	int n = payment_format_charge(frame, sizeof(frame), l->uuid, l->plate, l->amount);

	l->sends++;
	l->deadline_ms = now_ms + PAYMENT_SEND_MS;
	if (n > 0) {
		(void)hw_uart_send(UART_PAYMENT, frame, (size_t)n);
	}
}

/* ---- Assistência (REQ-09) ----------------------------------------------- */

/**
 * Entra em assistência: motor parado, verde apagado, vermelho aceso e um
 * pulso no buzzer (só na primeira entrada; uma falha a mais dentro da
 * assistência só pode torná-la travada).
 */
static void assist(struct lane *l, bool latch, const char *reason, int64_t now_ms)
{
	(void)hw_motor(l->id, MOTOR_OFF);
	(void)hw_lights(l->id, true, false);

	if (l->uuid_active) {
		cancel_payment(l);
	}
	if (l->state != LANE_ASSIST) {
		printk("[%lld] %s: assistance (%s, %s)\n", (long long)now_ms, lane_names[l->id],
		       reason, latch ? "latched" : "rearmable");
		(void)hw_buzzer(l->id, true);
		l->buzzer_on = true;
		l->buzzer_off_ms = now_ms + BUZZER_PULSE_MS;
		enter(l, LANE_ASSIST, now_ms);
	}
	l->latched = l->latched || latch;
}

/* ---- Movimento da cancela (REQ-06) -------------------------------------- */

/** Liga o motor e começa o prazo de 2500 ms. Erro de GPIO -> travada. */
static void start_motor(struct lane *l, enum motor_cmd cmd, enum lane_state next,
			int64_t now_ms)
{
	if (hw_motor(l->id, cmd) != 0) {
		assist(l, true, "motor gpio", now_ms);
		return;
	}
	l->deadline_ms = now_ms + MOTOR_BUDGET_MS;
	enter(l, next, now_ms);
}

/** REQ-08: verde apagado e vermelho aceso ANTES de fechar. */
static void start_close(struct lane *l, int64_t now_ms)
{
	(void)hw_lights(l->id, true, false);
	start_motor(l, MOTOR_CLOSE, LANE_CLOSING, now_ms);
}

/* ---- Câmera (REQ-02) ---------------------------------------------------- */

/** Nova tentativa: ID novo, AT+CAPTURE, prazo de 1000 ms. */
static void start_attempt(struct lane *l, int64_t now_ms)
{
	char frame[32];
	int n;

	l->attempts++;
	l->capture_id = next_capture_id;
	next_capture_id = (next_capture_id >= CAPTURE_ID_MAX) ? 1U : next_capture_id + 1U;
	l->deadline_ms = now_ms + CAMERA_ATTEMPT_MS;

	n = camera_format_capture(frame, sizeof(frame), l->capture_id);
	if (n > 0) {
		(void)hw_uart_send(l->id == LANE_ENTRY ? UART_CAM_ENTRY : UART_CAM_EXIT, frame,
				   (size_t)n);
	}
	if (l->state != LANE_IDENTIFYING) {
		enter(l, LANE_IDENTIFYING, now_ms);
	}
}

/** Tentativa perdida (confiança baixa ou prazo): próxima ou assistência. */
static void attempt_failed(struct lane *l, int64_t now_ms)
{
	if (l->attempts < CAMERA_MAX_ATTEMPTS) {
		start_attempt(l, now_ms);
	} else {
		assist(l, false, "camera attempts exhausted", now_ms);
	}
}

/** Placa aceita: decide entre abrir, cobrar ou pedir assistência (REQ-04). */
static void authorize(struct lane *l, int64_t now_ms)
{
	if (l->id == LANE_ENTRY) {
		if (sessions_contains(l->plate)) {
			assist(l, false, "plate already parked", now_ms);
		} else if (!sessions_has_space()) {
			assist(l, false, "lot full", now_ms);
		} else {
			start_motor(l, MOTOR_OPEN, LANE_OPENING, now_ms);
		}
		return;
	}
	if (!sessions_contains(l->plate)) {
		assist(l, false, "exit without session", now_ms);
	} else if (ui_request_plate(l->plate) != 0) {
		assist(l, false, "display request", now_ms);
	} else {
		l->deadline_ms = now_ms + DISPLAY_BUDGET_MS;
		enter(l, LANE_SHOW_PLATE, now_ms);
	}
}

/* ---- API ---------------------------------------------------------------- */

void lane_init(struct lane *l, enum lane_id id, int64_t now_ms)
{
	memset(l, 0, sizeof(*l));
	l->id = id;
	l->state = LANE_STARTUP;
	l->deadline_ms = now_ms + STARTUP_SETTLE_MS;
	/* REQ-01: motor desligado, vermelho aceso, verde e buzzer apagados. */
	(void)hw_motor(id, MOTOR_OFF);
	(void)hw_lights(id, true, false);
	(void)hw_buzzer(id, false);
}

void lane_fault(struct lane *l, int64_t now_ms)
{
	assist(l, true, "adc fault", now_ms);
}

void lane_tick(struct lane *l, int64_t now_ms)
{
	const bool upper = hw_limit_upper(l->id);
	const bool lower = hw_limit_lower(l->id);
	const bool beam = hw_beam(l->id);
	const bool closed = lower && !upper;

	/* O pulso do buzzer acaba sozinho, mesmo na assistência travada. */
	if (l->buzzer_on && now_ms >= l->buzzer_off_ms) {
		(void)hw_buzzer(l->id, false);
		l->buzzer_on = false;
	}

	/* REQ-01: superior e inferior juntos é impossível: trava até o reset. */
	if (upper && lower) {
		assist(l, true, "contradictory limits", now_ms);
		return;
	}

	switch (l->state) {
	case LANE_STARTUP:
		if (now_ms >= l->deadline_ms) {
			if (closed) {
				enter(l, LANE_IDLE, now_ms);
			} else {
				/* Sem inferior em repouso: assistência, nunca "voltar para casa". */
				assist(l, true, "not closed at startup", now_ms);
			}
		}
		break;

	case LANE_IDLE:
	case LANE_IDENTIFYING:
	case LANE_SHOW_PLATE:
	case LANE_SHOW_AMOUNT:
	case LANE_PAYING:
		if (!closed) {
			assist(l, true, "barrier left closed position", now_ms);
		} else if (l->state == LANE_IDLE || now_ms < l->deadline_ms) {
			/* nada a fazer: esperando evento ou prazo */
		} else if (l->state == LANE_IDENTIFYING) {
			attempt_failed(l, now_ms); /* prazo de 1000 ms da captura */
		} else if (l->state == LANE_PAYING && l->sends < PAYMENT_MAX_SENDS) {
			send_charge(l, now_ms); /* reenvio: mesmo UUID, mesmo valor */
		} else {
			assist(l, false, "payment or display timeout", now_ms);
		}
		break;

	case LANE_OPENING:
		if (upper) {
			/* Motor desligado PRIMEIRO; só então o verde (REQ-06). */
			if (hw_motor(l->id, MOTOR_OFF) != 0) {
				assist(l, true, "motor gpio", now_ms);
				break;
			}
			(void)hw_lights(l->id, false, true);
			l->beam_seen = false;
			l->deadline_ms = now_ms + PASSAGE_BUDGET_MS;
			enter(l, LANE_OPEN, now_ms);
		} else if (now_ms >= l->deadline_ms) {
			assist(l, true, "open timeout", now_ms); /* travamento ou superior ausente */
		}
		break;

	case LANE_OPEN:
		if (!upper) {
			assist(l, true, "lost upper limit", now_ms);
			break;
		}
		if (beam) {
			l->beam_seen = true;
		}
		if (now_ms >= l->deadline_ms) {
			/* REQ-07: 10 s sem passagem completa. Aberta: travada. */
			assist(l, true, "passage timeout", now_ms);
		}
		break;

	case LANE_CLOSING:
		if (beam) {
			/* REQ-08: obstrução no fechamento. Para, sem reverter. */
			assist(l, true, "obstruction while closing", now_ms);
		} else if (lower) {
			if (hw_motor(l->id, MOTOR_OFF) != 0) {
				assist(l, true, "motor gpio", now_ms);
			} else {
				enter(l, LANE_IDLE, now_ms);
			}
		} else if (now_ms >= l->deadline_ms) {
			assist(l, true, "close timeout", now_ms); /* inferior ausente */
		}
		break;

	case LANE_ASSIST:
		break; /* rearme em lane_on_distance */
	}
}

void lane_on_distance(struct lane *l, uint16_t code, int64_t now_ms)
{
	switch (l->state) {
	case LANE_IDLE:
		/* REQ-02: chegada com a cancela fechada (o tick já garantiu). */
		if (code < DIST_ARRIVE_BELOW) {
			l->attempts = 0;
			start_attempt(l, now_ms);
		}
		break;

	case LANE_IDENTIFYING:
		/* REQ-03: o carro foi embora: abandona a captura, sem assistência. */
		if (code > DIST_LEAVE_ABOVE) {
			enter(l, LANE_IDLE, now_ms);
		}
		break;

	case LANE_SHOW_PLATE:
	case LANE_SHOW_AMOUNT:
	case LANE_PAYING:
		/* REQ-03/05: saiu durante o pagamento: invalida, cancela, assistência. */
		if (code > DIST_LEAVE_ABOVE) {
			assist(l, false, "left during payment", now_ms);
		}
		break;

	case LANE_OPEN:
		/* REQ-07: passagem completa = feixe ativo, depois livre, e carro longe. */
		if (l->beam_seen && code > DIST_LEAVE_ABOVE && !hw_beam(l->id)) {
			if (l->id == LANE_ENTRY) {
				/* Já verificado na autorização (sem sessão e com vaga). */
				(void)sessions_add(l->plate, now_ms);
			} else {
				(void)sessions_remove(l->plate);
			}
			printk("[%lld] %s: passage %s, %u parked\n", (long long)now_ms,
			       lane_names[l->id], l->plate, (unsigned int)sessions_count());
			start_close(l, now_ms);
		}
		break;

	case LANE_ASSIST:
		/* REQ-09: rearme só com a cancela fechada, carro longe e feixe livre. */
		if (!l->latched && code > DIST_LEAVE_ABOVE && !hw_beam(l->id) && closed_ok(l)) {
			enter(l, LANE_IDLE, now_ms);
		}
		break;

	case LANE_STARTUP:
	case LANE_OPENING:
	case LANE_CLOSING:
		break; /* a distância não muda nada nestes estados */
	}
}

void lane_on_camera_line(struct lane *l, const char *line, int64_t now_ms)
{
	struct plate_result r;

	/* OK e ERROR não são resultado e não reiniciam o prazo. */
	if (l->state != LANE_IDENTIFYING || camera_parse_plate(line, &r) != 0 ||
	    r.capture_id != l->capture_id) {
		return; /* fora de hora, malformada ou de outra tentativa */
	}
	if (r.confidence < CAMERA_MIN_CONFIDENCE) {
		attempt_failed(l, now_ms); /* confiança baixa: próxima tentativa já */
		return;
	}
	memcpy(l->plate, r.plate, sizeof(l->plate));
	printk("[%lld] %s: plate %s (%u)\n", (long long)now_ms, lane_names[l->id], l->plate,
	       (unsigned int)r.confidence);
	authorize(l, now_ms);
}

void lane_on_display_done(struct lane *l, bool ok, int64_t now_ms)
{
	if (l->state != LANE_SHOW_PLATE && l->state != LANE_SHOW_AMOUNT) {
		return; /* etapa de uma tentativa já abandonada */
	}
	if (!ok) {
		assist(l, false, "display error", now_ms);
		return;
	}
	if (l->state == LANE_SHOW_PLATE) {
		/*
		 * REQ-12: o valor é calculado UMA vez, aqui (depois da placa
		 * desenhada, antes do valor e do CHARGE), e não muda mais.
		 */
		int64_t entry_ms = 0;

		if (sessions_entry_time(l->plate, &entry_ms) != 0 ||
		    tariff_amount(now_ms - entry_ms, CONFIG_PARKING_TARIFF_UNIT_MS, &l->amount) !=
			    0) {
			assist(l, false, "tariff", now_ms);
		} else if (ui_request_amount(l->amount) != 0) {
			assist(l, false, "display request", now_ms);
		} else {
			l->deadline_ms = now_ms + DISPLAY_BUDGET_MS;
			enter(l, LANE_SHOW_AMOUNT, now_ms);
		}
		return;
	}
	/* Valor desenhado: UUID novo para esta tentativa e primeiro envio. */
	payment_sequence++;
	uuid_make(l->uuid, payment_sequence, (uint32_t)now_ms);
	l->uuid_active = true;
	l->sends = 0;
	send_charge(l, now_ms);
	enter(l, LANE_PAYING, now_ms);
}

void lane_on_payment_line(struct lane *l, const char *line, int64_t now_ms)
{
	struct payment_result r;

	/*
	 * Só vale enquanto esperamos o resultado (PAYING), com o UUID atual.
	 * Fora de PAYING o UUID já foi invalidado (uuid_active == false).
	 */
	if (l->state != LANE_PAYING || payment_parse_result(line, &r) != 0 ||
	    strcmp(r.uuid, l->uuid) != 0) {
		return; /* fora de hora, malformada ou de cobrança antiga */
	}
	if (r.status == PAYMENT_CANCELLED) {
		assist(l, false, "payment cancelled", now_ms);
	} else if (r.amount == l->amount) {
		l->uuid_active = false; /* usado: não pode autorizar de novo */
		printk("[%lld] exit: paid %u\n", (long long)now_ms, (unsigned int)l->amount);
		start_motor(l, MOTOR_OPEN, LANE_OPENING, now_ms);
	}
	/* PAID com outro valor: ignorado, nunca abre. */
}
