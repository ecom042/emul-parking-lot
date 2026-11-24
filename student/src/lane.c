/**
 * @file lane.c
 * @brief Máquina de estados das faixas.
 */

#include "lane.h"

#include <errno.h>
#include <stdbool.h>

#include "camera.h"
#include "display.h"
#include "distance.h"
#include "line.h"
#include "payment.h"
#include "session.h"
#include "tariff.h"

/** @brief Amplitude máxima do ruído do modelo, em códigos ADC. */
#define ARRIVAL_NOISE_CODES 64
#define ARRIVAL_SAMPLE_MS 10
#define ARRIVAL_SAMPLE_COUNT 16
#define DISPLAY_STAGE_TIMEOUT_MS 1000

/** @brief Estado de uma faixa. */
struct lane {
	enum hw_lane id;                  /**< Qual faixa. */
	enum lane_state state;            /**< Estado atual. */
	int64_t since_ms;                 /**< Início do prazo do estado atual. */
	int64_t buzzer_off_ms;            /**< Quando desligar o buzzer; 0 se desligado. */
	int io_error;                     /**< Último erro de escrita numa saída. */
	struct line_framer camera;        /**< Montador de quadros da câmera. */
	uint32_t capture_id;              /**< ID da última captura pedida. */
	unsigned int attempts;            /**< Tentativas de captura deste veículo. */
	int64_t arrival_sample_ms;         /**< Instante da última leitura para o filtro. */
	unsigned int arrival_samples;    /**< Leituras acumuladas perto do limiar. */
	unsigned int arrival_sum;        /**< Soma das leituras perto do limiar. */
	uint32_t charge_amount;           /**< Valor congelado após o desenho da placa. */
	char plate[CAMERA_PLATE_LEN + 1]; /**< Placa aceita. */
};

static struct lane lanes[HW_LANE_COUNT];

/** @brief Muda de estado e reinicia o prazo. */
static void enter(struct lane *l, enum lane_state state, int64_t now_ms)
{
	l->state = state;
	l->since_ms = now_ms;
	l->arrival_samples = 0;
}

/** @brief Indica se o prazo do estado atual venceu. */
static bool expired(const struct lane *l, int64_t now_ms, int64_t budget_ms)
{
	return now_ms - l->since_ms >= budget_ms;
}

/** @brief Escreve uma saída; um erro fica guardado e trava a faixa no próximo passo. */
static int drive(struct lane *l, enum hw_output output, bool on)
{
	int err = hw_output_set(l->id, output, on);

	if (err < 0) {
		l->io_error = err;
	}
	return err;
}

/** @brief Confirma chegada com uma leitura inequívoca ou pela média perto do limiar. */
static bool vehicle_present(struct lane *l, int64_t now_ms)
{
	uint16_t raw;

	if (distance_get(l->id, now_ms, &raw) < 0 ||
	    raw >= DISTANCE_ARRIVAL_CODE + ARRIVAL_NOISE_CODES) {
		l->arrival_samples = 0;
		return false;
	}
	if (raw < DISTANCE_ARRIVAL_CODE - ARRIVAL_NOISE_CODES) {
		return true;
	}
	if (l->arrival_samples == 0) {
		l->arrival_sum = 0;
	} else if (now_ms - l->arrival_sample_ms < ARRIVAL_SAMPLE_MS) {
		return false;
	}
	l->arrival_sample_ms = now_ms;
	l->arrival_sum += raw;
	l->arrival_samples++;
	if (l->arrival_samples < ARRIVAL_SAMPLE_COUNT) {
		return false;
	}
	l->arrival_samples = 0;
	return l->arrival_sum < ARRIVAL_SAMPLE_COUNT * DISTANCE_ARRIVAL_CODE;
}

/** @brief Há amostra de distância atual acima do limiar de saída. */
static bool vehicle_gone(const struct lane *l, int64_t now_ms)
{
	uint16_t raw;

	return distance_get(l->id, now_ms, &raw) == 0 && raw > DISTANCE_CLEAR_CODE;
}

/**
 * @brief Para o motor e pede assistência.
 *
 * Desliga o motor antes de tudo, apaga o verde, acende o vermelho e pulsa o
 * buzzer uma vez. Na saída, invalida a cobrança antes de cancelá-la.
 *
 * @param latched true para travar até o reset; false para assistência com a
 *        cancela fechada, que rearma quando o veículo vai embora.
 */
static void stop(struct lane *l, int64_t now_ms, bool latched)
{
	bool pulse = l->state != LANE_ASSIST;
	int motor_rc;
	int green_rc;
	int red_rc;

	if (l->state == LANE_FAULT) {
		return;
	}
	motor_rc = drive(l, HW_OUT_MOTOR_ENABLE, false);
	green_rc = drive(l, HW_OUT_GREEN, false);
	red_rc = drive(l, HW_OUT_RED, true);

	if (motor_rc < 0 || green_rc < 0 || red_rc < 0) {
		latched = true;
	}
	if (pulse && drive(l, HW_OUT_BUZZER, true) == 0) {
		l->buzzer_off_ms = now_ms + LANE_BUZZER_PULSE_MS;
	}
	if (l->id == HW_LANE_EXIT && payment_abort() < 0) {
		latched = true;
	}
	enter(l, latched ? LANE_FAULT : LANE_ASSIST, now_ms);
}

/** @brief Desliga o buzzer quando o pulso termina. */
static void service_buzzer(struct lane *l, int64_t now_ms)
{
	if (l->buzzer_off_ms != 0 && now_ms >= l->buzzer_off_ms &&
	    drive(l, HW_OUT_BUZZER, false) == 0) {
		l->buzzer_off_ms = 0;
	}
}

/** @brief Lê os bytes da câmera e devolve o próximo quadro completo, ou NULL. */
static const char *camera_rx(struct lane *l, int64_t now_ms)
{
	enum hw_uart uart = l->id == HW_LANE_ENTRY ? HW_UART_CAMERA_ENTRY : HW_UART_CAMERA_EXIT;
	uint8_t byte;

	for (size_t remaining = LINE_MAX_FRAME; remaining > 0 && hw_uart_read(uart, &byte) == 0;
	     remaining--) {
		if (line_feed(&l->camera, byte, now_ms) == LINE_READY) {
			return line_frame(&l->camera);
		}
	}
	return NULL;
}

/** @brief Pede a próxima captura, com ID novo, ou a assistência se acabaram. */
static void next_attempt(struct lane *l, int64_t now_ms)
{
	enum hw_uart uart = l->id == HW_LANE_ENTRY ? HW_UART_CAMERA_ENTRY : HW_UART_CAMERA_EXIT;

	if (l->attempts == LANE_CAPTURE_ATTEMPTS) {
		stop(l, now_ms, false);
		return;
	}
	l->attempts++;
	l->capture_id++;
	if (camera_request(uart, l->capture_id) < 0) {
		stop(l, now_ms, false);
		return;
	}
	enter(l, LANE_IDENTIFYING, now_ms);
}

/** @brief Liga o motor no sentido de abrir. O vermelho segue aceso. */
static void open_barrier(struct lane *l, int64_t now_ms)
{
	if (drive(l, HW_OUT_MOTOR_DIRECTION, true) < 0 || drive(l, HW_OUT_MOTOR_ENABLE, true) < 0) {
		stop(l, now_ms, true);
		return;
	}
	enter(l, LANE_OPENING, now_ms);
}

/**
 * @brief Decide o que fazer com uma placa aceita.
 *
 * Na entrada, abre se a placa não está estacionada e há vaga. Na saída,
 * exige sessão ativa e inicia o desenho da placa. prepare_charge() congela
 * o valor depois que o SPI termina de desenhá-la.
 */
static void accept_plate(struct lane *l, int64_t now_ms)
{
	if (l->id == HW_LANE_ENTRY) {
		if (session_find(l->plate, NULL) == 0 ||
		    session_count() >= CONFIG_PARKING_CAPACITY) {
			stop(l, now_ms, false);
		} else {
			open_barrier(l, now_ms);
		}
		return;
	}
	if (session_find(l->plate, NULL) < 0 || display_show_plate(l->plate) < 0) {
		stop(l, now_ms, false);
		return;
	}
	enter(l, LANE_SHOWING_PLATE, now_ms);
}

/** @brief Avança a preparação da cobrança depois que o SPI completa cada etapa. */
static void prepare_charge(struct lane *l, int64_t now_ms)
{
	int64_t entry_ms;
	int status;

	if (vehicle_gone(l, now_ms) || expired(l, now_ms, DISPLAY_STAGE_TIMEOUT_MS)) {
		stop(l, now_ms, false);
		return;
	}
	status = display_status();
	if (status == -EAGAIN) {
		return;
	}
	if (status < 0) {
		stop(l, now_ms, false);
		return;
	}
	if (l->state == LANE_SHOWING_PLATE) {
		if (session_find(l->plate, &entry_ms) < 0 ||
		    tariff_compute(now_ms - entry_ms, CONFIG_PARKING_TARIFF_UNIT_MS,
				   &l->charge_amount) < 0 ||
		    display_show_charge(l->charge_amount) < 0) {
			stop(l, now_ms, false);
			return;
		}
		enter(l, LANE_SHOWING_CHARGE, now_ms);
	} else if (payment_start(l->plate, l->charge_amount, now_ms) < 0) {
		stop(l, now_ms, false);
	} else {
		enter(l, LANE_PAYING, now_ms);
	}
}

/** @brief Processa partida antes de autorizar o PAID com distância atual. */
static void service_payment(struct lane *l, int64_t now_ms)
{
	uint16_t raw;
	bool fresh = distance_get(l->id, now_ms, &raw) == 0;
	enum payment_event result;

	if (fresh && raw > DISTANCE_CLEAR_CODE) {
		stop(l, now_ms, false);
		return;
	}
	result = payment_poll(now_ms, fresh);
	if (result == PAYMENT_EV_FAILED) {
		stop(l, now_ms, false);
	} else if (result == PAYMENT_EV_PAID) {
		open_barrier(l, now_ms);
	}
}

/** @brief Trata a identificação: abandono, resultado da câmera e prazo. */
static void identify(struct lane *l, int64_t now_ms)
{
	const char *frame;
	uint16_t raw;
	unsigned int confidence;

	if (vehicle_gone(l, now_ms)) {
		enter(l, LANE_IDLE, now_ms);
		return;
	}
	if (expired(l, now_ms, LANE_CAPTURE_TIMEOUT_MS)) {
		next_attempt(l, now_ms);
		return;
	}
	frame = camera_rx(l, now_ms);
	/* Um resultado só autoriza com amostra de distância atual. */
	if (frame != NULL && distance_get(l->id, now_ms, &raw) == 0 &&
	    camera_parse_plate(frame, l->capture_id, l->plate, &confidence) == 0) {
		if (confidence >= CAMERA_MIN_CONFIDENCE) {
			accept_plate(l, now_ms);
		} else {
			next_attempt(l, now_ms);
		}
	}
}

/** @brief No fim de curso superior: motor desligado antes de acender o verde. */
static void opened(struct lane *l, int64_t now_ms)
{
	if (drive(l, HW_OUT_MOTOR_ENABLE, false) < 0 || drive(l, HW_OUT_RED, false) < 0 ||
	    drive(l, HW_OUT_GREEN, true) < 0) {
		stop(l, now_ms, true);
		return;
	}
	enter(l, LANE_OPEN, now_ms);
}

/**
 * @brief Acompanha a passagem: feixe ativo, depois livre com o veículo longe.
 *
 * Só a passagem completa abre (entrada) ou encerra (saída) a sessão. O prazo
 * conta desde a abertura e vale também com o feixe interrompido.
 */
static void passage(struct lane *l, int beam, int64_t now_ms)
{
	int err;

	if (expired(l, now_ms, LANE_PASSAGE_BUDGET_MS)) {
		stop(l, now_ms, true);
		return;
	}
	if (beam) {
		l->state = LANE_PASSING;
	} else if (l->state == LANE_PASSING && vehicle_gone(l, now_ms)) {
		err = l->id == HW_LANE_ENTRY ? session_add(l->plate, now_ms)
					     : session_remove(l->plate);
		if (err < 0 || drive(l, HW_OUT_GREEN, false) < 0 || drive(l, HW_OUT_RED, true) < 0 ||
		    drive(l, HW_OUT_MOTOR_DIRECTION, false) < 0 ||
		    hw_input_get(l->id, HW_IN_BEAM) != 0 ||
		    drive(l, HW_OUT_MOTOR_ENABLE, true) < 0) {
			stop(l, now_ms, true);
			return;
		}
		enter(l, LANE_CLOSING, now_ms);
	}
}

/** @brief Acompanha o fechamento; obstrução para o motor, sem reverter. */
static void closing(struct lane *l, int beam, int lower, int64_t now_ms)
{
	if (beam || expired(l, now_ms, LANE_MOTOR_BUDGET_MS)) {
		stop(l, now_ms, true);
	} else if (lower) {
		if (drive(l, HW_OUT_MOTOR_ENABLE, false) < 0) {
			stop(l, now_ms, true);
			return;
		}
		enter(l, LANE_IDLE, now_ms);
	}
}

void lane_init(enum hw_lane lane, int64_t now_ms)
{
	struct lane *l = &lanes[lane];

	*l = (struct lane){.id = lane};
	line_init(&l->camera, true);
	enter(l, LANE_BOOT, now_ms);
}

void lane_fail(enum hw_lane lane, int64_t now_ms)
{
	stop(&lanes[lane], now_ms, true);
}

void lane_step(enum hw_lane lane, int64_t now_ms)
{
	struct lane *l = &lanes[lane];
	int upper = hw_input_get(lane, HW_IN_LIMIT_UPPER);
	int lower = hw_input_get(lane, HW_IN_LIMIT_LOWER);
	int beam = hw_input_get(lane, HW_IN_BEAM);
	bool identifying = l->state == LANE_IDENTIFYING;

	service_buzzer(l, now_ms);
	if (upper < 0 || lower < 0 || beam < 0 || l->io_error < 0 || distance_faulted() ||
	    (upper && lower)) {
		stop(l, now_ms, true);
		return;
	}
	if (l->state == LANE_BOOT) {
		if (expired(l, now_ms, LANE_SETTLE_MS)) {
			enter(l, LANE_IDLE, now_ms);
		}
		return;
	}
	switch (l->state) {
	case LANE_IDLE:
		if (!lower) {
			stop(l, now_ms, true);
		} else if (vehicle_present(l, now_ms)) {
			l->attempts = 0;
			next_attempt(l, now_ms);
		}
		break;
	case LANE_IDENTIFYING:
		if (!lower) {
			stop(l, now_ms, true);
		} else {
			identify(l, now_ms);
		}
		break;
	case LANE_SHOWING_PLATE:
	case LANE_SHOWING_CHARGE:
	case LANE_PAYING:
		if (!lower) {
			stop(l, now_ms, true);
		} else if (l->state == LANE_PAYING) {
			service_payment(l, now_ms);
		} else {
			prepare_charge(l, now_ms);
		}
		break;
	case LANE_OPENING:
		if (expired(l, now_ms, LANE_MOTOR_BUDGET_MS)) {
			stop(l, now_ms, true);
		} else if (upper) {
			opened(l, now_ms);
		}
		break;
	case LANE_OPEN:
	case LANE_PASSING:
		if (!upper) {
			stop(l, now_ms, true);
		} else {
			passage(l, beam, now_ms);
		}
		break;
	case LANE_CLOSING:
		closing(l, beam, lower, now_ms);
		break;
	case LANE_ASSIST:
		if (!lower) {
			stop(l, now_ms, true);
		} else if (!beam && vehicle_gone(l, now_ms)) {
			enter(l, LANE_IDLE, now_ms);
		}
		break;
	case LANE_BOOT:
	case LANE_FAULT:
		break;
	}
	/* Descarta tráfego residual somente depois de atender as saídas. */
	if (!identifying && l->state != LANE_IDENTIFYING) {
		camera_rx(l, now_ms);
	}
	if (lane == HW_LANE_EXIT && l->state != LANE_PAYING) {
		payment_poll(now_ms, false);
	}
}

enum lane_state lane_state_get(enum hw_lane lane)
{
	return lanes[lane].state;
}
