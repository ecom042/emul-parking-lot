/*
 * Máquina de estados de uma faixa (REQ-01 a 04, 06 a 09). Ver lane.h.
 *
 * Ordem de um passo:
 *  1. cancela (prazos, fim de curso) e buzzer;
 *  2. falhas travadas (cancela, ADC);
 *  3. distância fresca e rearme;
 *  4. linhas recebidas da câmera;
 *  5. o estado atual decide.
 */
#include "lane.h"

#include <string.h>

#include <zephyr/sys/util.h>

#include "adc_sampler.h"
#include "config.h"
#include "display.h"
#include "sessions.h"
#include "tariff.h"

/* Câmera de cada faixa. */
static const enum hw_uart camera_uart[LANE_COUNT] = {
	[LANE_ENTRY] = HW_UART_CAMERA_ENTRY,
	[LANE_EXIT] = HW_UART_CAMERA_EXIT,
};

void lane_init(struct lane *l, enum lane_id id)
{
	memset(l, 0, sizeof(*l));
	l->id = id;
	l->state = LANE_BOOT;
	l->armed = true; /* um carro já parado na partida pode ser atendido */
	l->next_capture_id = 1;
	barrier_init(&l->barrier, id);
	lp_init(&l->cam_parser, true);
	lp_init(&l->pay_parser, false);
}

/*
 * REQ-05: invalida a cobrança ativa ANTES de mandar o CANCEL. Depois disso
 * nenhuma resposta, nem a deste UUID, autoriza nada.
 */
static void cancel_charge(struct lane *l)
{
	char cmd[UART_FRAME_MAX];
	char old[UUID_BUF];
	int n;

	if (l->uuid[0] == '\0') {
		return;
	}
	memcpy(old, l->uuid, sizeof(old));
	l->uuid[0] = '\0';
	n = payment_format_cancel(cmd, sizeof(cmd), old);
	if (n > 0) {
		(void)hw_uart_write(HW_UART_PAYMENT, cmd, (size_t)n);
	}
}

/*
 * REQ-09: assistência. Motor parado, verde apagado, vermelho aceso e um
 * pulso de BUZZER_PULSE_MS. Se a faixa já está em assistência, não repete o
 * pulso; uma falha travada só promove ASSIST para LOCKED.
 * Qualquer pedido pendente à câmera é invalidado.
 * Erros de driver aqui são ignorados de propósito: não há o que fazer além
 * de tentar deixar tudo no estado seguro.
 */
static void assist(struct lane *l, bool latched, int64_t now_ms)
{
	bool already = l->state == LANE_ASSIST || l->state == LANE_LOCKED;

	/*
	 * Se a cancela estava aberta esperando o carro passar, ele pode terminar
	 * a passagem depois (por exemplo, parado no feixe além do prazo). Essa
	 * passagem continua valendo para as sessões; ver late_passage_step.
	 */
	if (l->state == LANE_PASSAGE) {
		l->passage_late = true;
	}
	if (l->barrier.state != BARRIER_FAULT && l->barrier.state != BARRIER_CLOSED) {
		/* Cancela fora de 0°: falha de posição, sempre travada. */
		barrier_fail(&l->barrier, BARRIER_FAULT_NONE);
		latched = true;
	}
	(void)hw_out_set(l->id, HW_OUT_ENABLE, false);
	(void)hw_out_set(l->id, HW_OUT_GREEN, false);
	(void)hw_out_set(l->id, HW_OUT_RED, true);
	if (!already) {
		(void)hw_out_set(l->id, HW_OUT_BUZZER, true);
		l->buzzer_on = true;
		l->buzzer_off_ms = now_ms + BUZZER_PULSE_MS;
	}
	l->capture_id = 0;
	cancel_charge(l);
	if (latched || l->state == LANE_LOCKED) {
		l->state = LANE_LOCKED;
	} else {
		l->state = LANE_ASSIST;
	}
}

void lane_start(struct lane *l, int64_t now_ms)
{
	ARG_UNUSED(now_ms);
	(void)hw_out_set(l->id, HW_OUT_RED, true);
}

/* Conferência dos fins de curso, uma vez, depois da acomodação. */
static void boot_check(struct lane *l, int64_t now_ms)
{
	if (now_ms < STARTUP_SETTLE_MS) {
		return;
	}
	if (barrier_start(&l->barrier) == BARRIER_CLOSED) {
		l->state = LANE_IDLE;
	} else {
		/* Sem inferior, contraditórios ou erro: travado até o reset. */
		assist(l, true, now_ms);
	}
}

/* Desliga o buzzer quando o pulso termina. */
static void buzzer_service(struct lane *l, int64_t now_ms)
{
	if (l->buzzer_on && now_ms >= l->buzzer_off_ms) {
		(void)hw_out_set(l->id, HW_OUT_BUZZER, false);
		l->buzzer_on = false;
	}
}

/* REQ-02: dispara uma tentativa de captura com um ID novo. */
static void start_capture(struct lane *l, int64_t now_ms)
{
	char cmd[32];
	int n;

	l->capture_id = l->next_capture_id;
	l->next_capture_id = l->next_capture_id >= CAMERA_ID_MAX ? CAMERA_ID_MIN
								 : l->next_capture_id + 1;
	l->deadline_ms = now_ms + CAMERA_TIMEOUT_MS;
	l->state = LANE_IDENTIFY;

	n = camera_format_capture(cmd, sizeof(cmd), l->capture_id);
	if (n < 0 || hw_uart_write(camera_uart[l->id], cmd, (size_t)n) != 0) {
		assist(l, true, now_ms);
	}
}

/* Tentativa falhou (confiança baixa ou prazo): próxima ou assistência. */
static void next_attempt(struct lane *l, int64_t now_ms)
{
	if (l->attempt >= CAMERA_MAX_ATTEMPTS) {
		assist(l, false, now_ms);
		return;
	}
	l->attempt++;
	start_capture(l, now_ms);
}

/* Envia (ou reenvia) exatamente a mesma linha de cobrança. */
static void send_charge(struct lane *l, int64_t now_ms)
{
	l->sends++;
	l->deadline_ms = now_ms + PAYMENT_TIMEOUT_MS;
	if (hw_uart_write(HW_UART_PAYMENT, l->charge_line, l->charge_len) != 0) {
		assist(l, true, now_ms);
	}
}

/* Começa a abrir para a placa aceita. */
static void open_barrier(struct lane *l, int64_t now_ms)
{
	if (barrier_open(&l->barrier, now_ms) != 0) {
		assist(l, true, now_ms);
		return;
	}
	l->state = LANE_OPENING;
}

/*
 * REQ-05/12: começa a cobrança da saída. Ordem exigida: desenha a placa,
 * congela a tarifa, desenha valor e avisos e só então manda o CHARGE.
 */
static void start_payment(struct lane *l, int64_t entry_ms, int64_t now_ms)
{
	int n;

	if (display_show_plate(l->plate) != 0) {
		assist(l, false, now_ms);
		return;
	}
	if (tariff_compute(now_ms - entry_ms, CONFIG_PARKING_TARIFF_UNIT_MS, &l->amount) != 0) {
		/* Mais de TARIFF_MAX_UNITS unidades: não cobra, pede ajuda. */
		assist(l, false, now_ms);
		return;
	}
	if (display_show_amount(l->amount) != 0) {
		assist(l, false, now_ms);
		return;
	}
	payment_new_uuid(l->uuid, hw_entropy());
	n = payment_format_charge(l->charge_line, sizeof(l->charge_line), l->uuid, l->plate,
				  l->amount);
	if (n < 0) {
		assist(l, false, now_ms);
		return;
	}
	l->charge_len = (size_t)n;
	l->sends = 0;
	l->state = LANE_PAYING;
	send_charge(l, now_ms);
}

/* REQ-04: placa aceita; decide pelas sessões. */
static void plate_accepted(struct lane *l, const char *plate, int64_t now_ms)
{
	int64_t entry_ms;
	bool parked = sessions_find(plate, &entry_ms);

	strncpy(l->plate, plate, PLATE_LEN);
	l->plate[PLATE_LEN] = '\0';
	l->capture_id = 0; /* nenhuma outra resposta vale mais neste ciclo */

	if (l->id == LANE_ENTRY) {
		if (parked || !sessions_has_space()) {
			/* Já estacionada ou lotado: não abre. */
			assist(l, false, now_ms);
			return;
		}
		open_barrier(l, now_ms);
	} else {
		if (!parked) {
			/* Saída sem sessão (leitura errada): nunca cobra outro carro. */
			assist(l, false, now_ms);
			return;
		}
		start_payment(l, entry_ms, now_ms);
	}
}

/* Consome os bytes da câmera e trata as linhas completas. */
static void camera_service(struct lane *l, int64_t now_ms)
{
	uint8_t chunk[16];
	size_t n;

	while ((n = hw_uart_read(camera_uart[l->id], chunk, sizeof(chunk))) > 0) {
		for (size_t i = 0; i < n; i++) {
			struct camera_msg msg;

			if (lp_feed(&l->cam_parser, chunk[i], now_ms) != LP_LINE) {
				continue;
			}
			camera_parse(lp_line(&l->cam_parser), &msg);
			/* Só o +PLATE do pedido atual, enquanto identifica, conta. */
			if (l->state != LANE_IDENTIFY || msg.kind != CAMERA_PLATE ||
			    msg.id != l->capture_id) {
				continue;
			}
			if (msg.confidence >= CAMERA_MIN_CONFIDENCE) {
				plate_accepted(l, msg.plate, now_ms);
			} else {
				next_attempt(l, now_ms);
			}
		}
	}
}

/* Consome os bytes do terminal (só na saída) e trata as linhas completas. */
static void payment_service(struct lane *l, int64_t now_ms)
{
	uint8_t chunk[16];
	size_t n;

	while ((n = hw_uart_read(HW_UART_PAYMENT, chunk, sizeof(chunk))) > 0) {
		for (size_t i = 0; i < n; i++) {
			struct payment_msg msg;

			if (lp_feed(&l->pay_parser, chunk[i], now_ms) != LP_LINE) {
				continue;
			}
			payment_parse(lp_line(&l->pay_parser), &msg);
			/* Só vale a resposta da cobrança ativa, enquanto se espera. */
			if (l->state != LANE_PAYING || l->uuid[0] == '\0' ||
			    (msg.kind != PAYMENT_PAID && msg.kind != PAYMENT_CANCELLED) ||
			    strcmp(msg.uuid, l->uuid) != 0) {
				continue;
			}
			if (msg.kind == PAYMENT_PAID && msg.amount == l->amount) {
				/* PAID exato e atual: a cobrança termina aqui. */
				l->uuid[0] = '\0';
				open_barrier(l, now_ms);
			} else if (msg.kind == PAYMENT_CANCELLED) {
				assist(l, false, now_ms);
			}
			/* PAID com outro valor: ignora (não abre). */
		}
	}
}

/* REQ-05: prazo de resposta e abandono durante o pagamento. */
static void paying_step(struct lane *l, int64_t now_ms)
{
	if (l->dist_fresh && l->dist > DIST_ABSENT_ABOVE) {
		/* O carro foi embora: invalida, cancela e pede ajuda. */
		assist(l, false, now_ms);
	} else if (now_ms >= l->deadline_ms) {
		if (l->sends < PAYMENT_MAX_SENDS) {
			send_charge(l, now_ms);
		} else {
			assist(l, false, now_ms);
		}
	}
}

/*
 * REQ-07: a passagem terminou? Feixe já visto ativo, agora livre, e o carro
 * além do limiar de ausência. Só uma passagem completa mexe nas sessões:
 * cria a da entrada ou encerra a da saída.
 */
static bool passage_done(struct lane *l, int beam, int64_t now_ms)
{
	if (beam == 1) {
		l->beam_seen = true;
	}
	if (!(l->beam_seen && beam == 0 && l->dist_fresh && l->dist > DIST_ABSENT_ABOVE)) {
		return false;
	}
	if (l->id == LANE_ENTRY) {
		(void)sessions_add(l->plate, now_ms);
	} else {
		(void)sessions_remove(l->plate);
	}
	return true;
}

/*
 * Passagem que termina depois da assistência (faixa travada, cancela
 * aberta). O carro está fisicamente dentro (ou fora), então a sessão segue a
 * passagem real; se não seguisse, a lotação contada pelo firmware ficaria
 * diferente da do estacionamento. Nada aqui mexe na cancela: ela continua
 * parada até o reset.
 */
static void late_passage_step(struct lane *l, int64_t now_ms)
{
	uint16_t code;
	int beam;

	if (!l->passage_late) {
		return;
	}
	beam = hw_in_get(l->id, HW_IN_BEAM);
	if (beam < 0) {
		l->passage_late = false; /* sem leitura confiável, desiste */
		return;
	}
	l->dist_fresh = adc_sampler_take(l->id, &code);
	if (l->dist_fresh) {
		l->dist = code;
	}
	if (passage_done(l, beam, now_ms)) {
		l->passage_late = false;
	}
}

/* REQ-07: espera a passagem completa e então fecha. */
static void passage_step(struct lane *l, int64_t now_ms)
{
	int beam = hw_in_get(l->id, HW_IN_BEAM);

	if (beam < 0) {
		assist(l, true, now_ms);
		return;
	}
	if (passage_done(l, beam, now_ms)) {
		/* REQ-08: verde apagado e vermelho aceso antes de fechar. */
		(void)hw_out_set(l->id, HW_OUT_GREEN, false);
		(void)hw_out_set(l->id, HW_OUT_RED, true);
		if (barrier_close(&l->barrier, now_ms) != 0) {
			assist(l, true, now_ms);
			return;
		}
		l->state = LANE_CLOSING;
		return;
	}
	if (now_ms >= l->deadline_ms) {
		/* Cancela aberta sem passagem: travada, nunca fecha sozinha. */
		assist(l, true, now_ms);
	}
}

static void state_step(struct lane *l, int64_t now_ms)
{
	switch (l->state) {
	case LANE_IDLE:
		if (l->armed && l->dist_fresh && l->dist < DIST_PRESENT_BELOW) {
			l->armed = false;
			l->attempt = 1;
			start_capture(l, now_ms);
		}
		break;
	case LANE_IDENTIFY:
		if (l->dist_fresh && l->dist > DIST_ABSENT_ABOVE) {
			/* REQ-03: o carro foi embora; abandona e invalida o pedido. */
			l->capture_id = 0;
			l->state = LANE_IDLE;
		} else if (now_ms >= l->deadline_ms) {
			next_attempt(l, now_ms);
		}
		break;
	case LANE_PAYING:
		paying_step(l, now_ms);
		break;
	case LANE_OPENING:
		if (l->barrier.state == BARRIER_OPEN) {
			/* REQ-06: verde só com fim de curso superior e motor parado. */
			(void)hw_out_set(l->id, HW_OUT_RED, false);
			(void)hw_out_set(l->id, HW_OUT_GREEN, true);
			l->beam_seen = false;
			l->deadline_ms = now_ms + PASSAGE_TIMEOUT_MS;
			l->state = LANE_PASSAGE;
		}
		break;
	case LANE_PASSAGE:
		passage_step(l, now_ms);
		break;
	case LANE_CLOSING:
		if (l->barrier.state == BARRIER_CLOSED) {
			l->state = LANE_IDLE;
		}
		break;
	case LANE_ASSIST:
		/* REQ-09: rearme só com o carro longe e o feixe livre. */
		if (l->dist_fresh && l->dist > DIST_ABSENT_ABOVE &&
		    hw_in_get(l->id, HW_IN_BEAM) == 0) {
			l->state = LANE_IDLE;
		}
		break;
	case LANE_BOOT:
	case LANE_LOCKED:
		/* Tratados em lane_step antes de chegar aqui. */
		break;
	}
}

void lane_step(struct lane *l, int64_t now_ms)
{
	enum barrier_state bs;
	uint16_t code;

	if (l->state == LANE_BOOT) {
		boot_check(l, now_ms);
		return;
	}
	bs = barrier_step(&l->barrier, now_ms);
	buzzer_service(l, now_ms);

	if (l->state == LANE_LOCKED) {
		late_passage_step(l, now_ms);
		return;
	}
	if (bs == BARRIER_FAULT || adc_sampler_failed()) {
		/*
		 * Falha de cancela (posição ou leitura) ou do ADC: travada até o
		 * reset (REQ-09).
		 */
		assist(l, true, now_ms);
		return;
	}

	l->dist_fresh = adc_sampler_take(l->id, &code);
	if (l->dist_fresh) {
		l->dist = code;
		if (code > DIST_ABSENT_ABOVE) {
			l->armed = true; /* o carro anterior saiu */
		}
	}

	camera_service(l, now_ms);
	if (l->id == LANE_EXIT) {
		payment_service(l, now_ms);
	}
	state_step(l, now_ms);
}

void lane_on_input(struct lane *l)
{
	barrier_safety(&l->barrier);
}
