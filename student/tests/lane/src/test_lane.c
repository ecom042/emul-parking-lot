/*
 * Testes da máquina de estados da faixa (lane.c) com hardware e ADC falsos
 * e os módulos reais de câmera, montador de linhas e sessões.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "adc_sampler.h"
#include "config.h"
#include "hw_fake.h"
#include "lane.h"
#include "payment.h"
#include "sessions.h"
#include "tariff.h"

/* O amostrador do ADC é falso: a faixa só vê suas funções. */
FAKE_VALUE_FUNC(bool, adc_sampler_failed);
FAKE_VALUE_FUNC(bool, adc_sampler_take, enum lane_id, uint16_t *);

#define NEAR 546  /* ~800 mm: carro presente */
#define MID 800   /* ~1170 mm: entre os limiares */
#define FAR 1365  /* ~2000 mm: carro ausente */

static struct lane lane;
static uint16_t sim_dist[LANE_COUNT];
static int64_t t; /* relógio do teste */

/* Toda chamada entrega uma amostra fresca com sim_dist. */
static bool take_custom(enum lane_id id, uint16_t *code)
{
	*code = sim_dist[id];
	return true;
}

static int64_t clock_custom(void)
{
	return t;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	hw_fake_reset();
	hw_now_ms_fake.custom_fake = clock_custom;
	RESET_FAKE(adc_sampler_failed);
	RESET_FAKE(adc_sampler_take);
	adc_sampler_take_fake.custom_fake = take_custom;
	sessions_clear();
	sim_dist[LANE_ENTRY] = FAR;
	sim_dist[LANE_EXIT] = FAR;
	t = 0;
	lane_init(&lane, LANE_ENTRY);
}

ZTEST_SUITE(lane, NULL, NULL, before, NULL, NULL);

/* Avança o relógio em passos de TICK_MS por 'ms'. */
static void run(int64_t ms)
{
	int64_t end = t + ms;

	while (t < end) {
		t += TICK_MS;
		lane_step(&lane, t);
	}
}

/* Partida completa: vermelho e conferência dos fins de curso. */
static void boot(void)
{
	lane_start(&lane, 0);
	run(STARTUP_SETTLE_MS);
}

static enum hw_uart cam(void)
{
	return lane.id == LANE_ENTRY ? HW_UART_CAMERA_ENTRY : HW_UART_CAMERA_EXIT;
}

static void reply_plate(uint32_t id, const char *plate, int confidence)
{
	char line[64];

	snprintf(line, sizeof(line), "OK\r\n+PLATE:%u,\"%s\",%d\r\n", (unsigned int)id, plate,
		 confidence);
	fake_uart_rx(cam(), line);
}

/* Fins de curso do modelo: braço em cima ou embaixo (e a work avisa). */
static void arm_at_top(void)
{
	fake_in[lane.id][HW_IN_LOWER] = 0;
	fake_in[lane.id][HW_IN_UPPER] = 1;
	lane_on_input(&lane);
}

static void arm_at_bottom(void)
{
	fake_in[lane.id][HW_IN_UPPER] = 0;
	fake_in[lane.id][HW_IN_LOWER] = 1;
	lane_on_input(&lane);
}

/* Carro chega, placa aceita, cancela abre: termina em PASSAGE. */
static void enter_until_green(const char *plate)
{
	boot();
	sim_dist[lane.id] = NEAR;
	run(TICK_MS);
	zassert_equal(lane.state, LANE_IDENTIFY);
	reply_plate(lane.capture_id, plate, 93);
	run(TICK_MS);
	zassert_equal(lane.state, LANE_OPENING);
	arm_at_top();
	run(TICK_MS);
	zassert_equal(lane.state, LANE_PASSAGE);
}

/* ---------- partida e assistência (REQ-01, 09) ---------- */

ZTEST(lane, test_boot_waits_before_checking_limits)
{
	fake_in[LANE_ENTRY][HW_IN_LOWER] = 0; /* entradas ainda não acomodadas */
	lane_start(&lane, 0);
	lane_step(&lane, STARTUP_SETTLE_MS - 1);
	zassert_equal(lane.state, LANE_BOOT);
	zassert_true(fake_out[LANE_ENTRY][HW_OUT_RED]);
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_BUZZER], 0);

	fake_in[LANE_ENTRY][HW_IN_LOWER] = 1; /* agora o modelo chegou */
	lane_step(&lane, STARTUP_SETTLE_MS);
	zassert_equal(lane.state, LANE_IDLE);
}

ZTEST(lane, test_start_ok_is_idle_red_no_buzzer)
{
	boot();
	zassert_equal(lane.state, LANE_IDLE);
	zassert_true(fake_out[LANE_ENTRY][HW_OUT_RED]);
	zassert_false(fake_out[LANE_ENTRY][HW_OUT_GREEN]);
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_BUZZER], 0);
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_ENABLE], 0);
	zassert_false(fake_out[LANE_EXIT][HW_OUT_RED], "a outra faixa não é tocada");
}

ZTEST(lane, test_start_partial_is_locked_with_one_pulse)
{
	fake_in[LANE_ENTRY][HW_IN_LOWER] = 0;
	sim_dist[LANE_ENTRY] = NEAR; /* carro presente: mesmo assim, sem captura */
	boot();
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_true(fake_out[LANE_ENTRY][HW_OUT_BUZZER]);
	zassert_false(fake_out[LANE_ENTRY][HW_OUT_ENABLE]);

	run(BUZZER_PULSE_MS);
	zassert_false(fake_out[LANE_ENTRY][HW_OUT_BUZZER], "pulso terminou");
	run(5000);
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_BUZZER], 1);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_str_equal(fake_uart_tx[HW_UART_CAMERA_ENTRY], "", "sem captura");
}

ZTEST(lane, test_contradictory_later_locks_once)
{
	boot();
	fake_in[LANE_ENTRY][HW_IN_UPPER] = 1;
	lane_on_input(&lane);
	run(TICK_MS);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(lane.barrier.fault, BARRIER_FAULT_CONTRADICTORY);

	fake_in[LANE_ENTRY][HW_IN_UPPER] = 0; /* a falha some: segue travada */
	run(500);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_BUZZER], 1);
}

ZTEST(lane, test_assist_clears_green_even_if_driver_fails)
{
	boot();
	fake_out_err[LANE_ENTRY][HW_OUT_BUZZER] = -EIO;
	fake_in[LANE_ENTRY][HW_IN_LOWER] = 0;
	run(TICK_MS);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_false(fake_out[LANE_ENTRY][HW_OUT_GREEN]);
	zassert_true(fake_out[LANE_ENTRY][HW_OUT_RED]);
}

ZTEST(lane, test_adc_fault_locks_lane)
{
	boot();
	adc_sampler_failed_fake.return_val = true;
	run(TICK_MS);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_BUZZER], 1);
}

/* ---------- identificação (REQ-02, 03) ---------- */

ZTEST(lane, test_far_car_does_not_capture)
{
	boot();
	sim_dist[LANE_ENTRY] = MID; /* entre os limiares: nada muda */
	run(1000);
	zassert_equal(lane.state, LANE_IDLE);
	zassert_str_equal(fake_uart_tx[HW_UART_CAMERA_ENTRY], "");
}

ZTEST(lane, test_stale_sample_does_not_capture)
{
	boot();
	adc_sampler_take_fake.custom_fake = NULL; /* sem amostra fresca */
	sim_dist[LANE_ENTRY] = NEAR;
	run(500);
	zassert_equal(lane.state, LANE_IDLE);
}

ZTEST(lane, test_presence_sends_capture)
{
	boot();
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	zassert_equal(lane.state, LANE_IDENTIFY);
	zassert_str_equal(fake_uart_tx[HW_UART_CAMERA_ENTRY], "AT+CAPTURE=1\r\n");
}

ZTEST(lane, test_accepted_plate_opens_then_green)
{
	enter_until_green("ABC1D23");
	zassert_true(fake_out[LANE_ENTRY][HW_OUT_GREEN]);
	zassert_false(fake_out[LANE_ENTRY][HW_OUT_RED]);
	zassert_false(fake_out[LANE_ENTRY][HW_OUT_ENABLE], "motor parado antes do verde");
	zassert_str_equal(lane.plate, "ABC1D23");
}

ZTEST(lane, test_green_only_after_upper_limit)
{
	boot();
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	reply_plate(lane.capture_id, "ABC1D23", 93);
	run(2000); /* motor ligado, fim de curso ainda não chegou */
	zassert_equal(lane.state, LANE_OPENING);
	zassert_false(fake_out[LANE_ENTRY][HW_OUT_GREEN]);
}

ZTEST(lane, test_low_confidence_retries_with_new_ids_then_assist)
{
	boot();
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	reply_plate(1, "ABC1D23", 62);
	run(TICK_MS);
	reply_plate(2, "ABC1D23", 79);
	run(TICK_MS);
	zassert_str_equal(fake_uart_tx[HW_UART_CAMERA_ENTRY],
			  "AT+CAPTURE=1\r\nAT+CAPTURE=2\r\nAT+CAPTURE=3\r\n");
	reply_plate(3, "ABC1D23", 84);
	run(TICK_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_BUZZER], 1);
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_ENABLE], 0);
}

ZTEST(lane, test_third_attempt_can_succeed)
{
	boot();
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	reply_plate(1, "ABC1D23", 62);
	run(TICK_MS);
	reply_plate(2, "ABC1D23", 79);
	run(TICK_MS);
	reply_plate(3, "ABC1D23", 93);
	run(TICK_MS);
	zassert_equal(lane.state, LANE_OPENING);
}

ZTEST(lane, test_timeouts_consume_attempts)
{
	boot();
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	fake_uart_rx(HW_UART_CAMERA_ENTRY, "OK\r\n"); /* OK não é placa */
	run(CAMERA_TIMEOUT_MS - TICK_MS);
	zassert_str_equal(fake_uart_tx[HW_UART_CAMERA_ENTRY], "AT+CAPTURE=1\r\n");
	run(TICK_MS);
	zassert_str_equal(fake_uart_tx[HW_UART_CAMERA_ENTRY],
			  "AT+CAPTURE=1\r\nAT+CAPTURE=2\r\n");
	run(2 * CAMERA_TIMEOUT_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_BUZZER], 1);
}

ZTEST(lane, test_other_id_and_garbage_are_ignored)
{
	boot();
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	reply_plate(99, "ABC1D23", 93);                   /* outro ID */
	fake_uart_rx(HW_UART_CAMERA_ENTRY, "ERROR\r\n");  /* diagnóstico */
	fake_uart_rx(HW_UART_CAMERA_ENTRY, "+PLATE:1,\"abc1d23\",93\r\n");
	run(TICK_MS);
	zassert_equal(lane.state, LANE_IDENTIFY);
	zassert_equal(lane.attempt, 1);
}

ZTEST(lane, test_stationary_car_is_not_recaptured)
{
	boot();
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	run(3 * CAMERA_TIMEOUT_MS + TICK_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	run(9000); /* o carro continua parado ali */
	zassert_str_equal(fake_uart_tx[HW_UART_CAMERA_ENTRY],
			  "AT+CAPTURE=1\r\nAT+CAPTURE=2\r\nAT+CAPTURE=3\r\n");
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_BUZZER], 1);
}

ZTEST(lane, test_assist_rearms_when_car_leaves_with_beam_clear)
{
	boot();
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	run(3 * CAMERA_TIMEOUT_MS + TICK_MS);
	zassert_equal(lane.state, LANE_ASSIST);

	sim_dist[LANE_ENTRY] = FAR;
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 1; /* algo no feixe: ainda não */
	run(100);
	zassert_equal(lane.state, LANE_ASSIST);
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 0;
	run(TICK_MS);
	zassert_equal(lane.state, LANE_IDLE);

	/* Um novo carro chega: nova captura com ID novo. */
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	zassert_equal(lane.state, LANE_IDENTIFY);
	zassert_equal(lane.capture_id, 4);
}

ZTEST(lane, test_leaving_abandons_identification)
{
	boot();
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	sim_dist[LANE_ENTRY] = FAR;
	run(TICK_MS);
	zassert_equal(lane.state, LANE_IDLE);
	zassert_equal(lane.capture_id, 0);

	/* A resposta atrasada da tentativa abandonada não autoriza nada. */
	reply_plate(1, "ABC1D23", 93);
	run(100);
	zassert_equal(lane.state, LANE_IDLE);
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_ENABLE], 0);
}

ZTEST(lane, test_capture_id_wraps)
{
	boot();
	lane.next_capture_id = CAMERA_ID_MAX;
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	zassert_equal(lane.capture_id, CAMERA_ID_MAX);
	zassert_equal(lane.next_capture_id, CAMERA_ID_MIN);
}

ZTEST(lane, test_camera_write_error_locks)
{
	boot();
	hw_uart_write_fake.return_val = -EIO;
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	zassert_equal(lane.state, LANE_LOCKED);
}

/* ---------- sessões (REQ-04) ---------- */

ZTEST(lane, test_duplicate_plate_is_refused)
{
	zassert_ok(sessions_add("ABC1D23", 0));
	boot();
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	reply_plate(1, "ABC1D23", 93);
	run(TICK_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_ENABLE], 0);
}

ZTEST(lane, test_full_lot_is_refused)
{
	char plate[PLATE_BUF];

	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		snprintf(plate, sizeof(plate), "XYZ9Z%02d", i);
		zassert_ok(sessions_add(plate, 0));
	}
	boot();
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	reply_plate(1, "ABC1D23", 93);
	run(TICK_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_ENABLE], 0);
}

ZTEST(lane, test_exit_without_session_is_refused)
{
	lane_init(&lane, LANE_EXIT);
	boot();
	sim_dist[LANE_EXIT] = NEAR;
	run(TICK_MS);
	zassert_str_equal(fake_uart_tx[HW_UART_CAMERA_EXIT], "AT+CAPTURE=1\r\n");
	reply_plate(1, "ABC1D23", 93);
	run(TICK_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(fake_out_on_count[LANE_EXIT][HW_OUT_ENABLE], 0);
}

/* ---------- passagem e fechamento (REQ-07, 08) ---------- */

ZTEST(lane, test_full_passage_creates_session_and_closes)
{
	int64_t entry_ms;

	enter_until_green("ABC1D23");
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 1;
	sim_dist[LANE_ENTRY] = 0; /* no plano da cancela */
	run(500);
	zassert_equal(sessions_count(), 0, "passar pelo feixe não basta");
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 0;
	sim_dist[LANE_ENTRY] = MID; /* ainda perto do outro lado */
	run(100);
	zassert_equal(lane.state, LANE_PASSAGE);
	sim_dist[LANE_ENTRY] = FAR;
	run(TICK_MS);

	zassert_true(sessions_find("ABC1D23", &entry_ms));
	zassert_equal(entry_ms, t);
	zassert_equal(lane.state, LANE_CLOSING);
	zassert_false(fake_out[LANE_ENTRY][HW_OUT_GREEN]);
	zassert_true(fake_out[LANE_ENTRY][HW_OUT_RED]);
	zassert_false(fake_out[LANE_ENTRY][HW_OUT_DIRECTION]);
	zassert_true(fake_out[LANE_ENTRY][HW_OUT_ENABLE]);

	arm_at_bottom();
	run(TICK_MS);
	zassert_equal(lane.state, LANE_IDLE);
	zassert_false(fake_out[LANE_ENTRY][HW_OUT_ENABLE]);
}

ZTEST(lane, test_distance_alone_is_not_passage)
{
	enter_until_green("ABC1D23");
	sim_dist[LANE_ENTRY] = FAR; /* sem feixe */
	run(1000);
	zassert_equal(lane.state, LANE_PASSAGE);
	zassert_equal(sessions_count(), 0);
}

ZTEST(lane, test_passage_deadline_locks_open)
{
	enter_until_green("ABC1D23");
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 1; /* obstrução antes de fechar */
	sim_dist[LANE_ENTRY] = FAR;
	run(PASSAGE_TIMEOUT_MS - 2 * TICK_MS);
	zassert_equal(lane.state, LANE_PASSAGE);
	zassert_true(fake_out[LANE_ENTRY][HW_OUT_GREEN]);
	run(2 * TICK_MS);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_false(fake_out[LANE_ENTRY][HW_OUT_GREEN]);
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_ENABLE], 1, "não fecha");
	zassert_equal(sessions_count(), 0);
}

ZTEST(lane, test_late_passage_still_creates_session)
{
	int64_t entry_ms;

	/* Carro para no feixe além do prazo: assistência travada. */
	enter_until_green("ABC1D23");
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 1;
	sim_dist[LANE_ENTRY] = 0;
	run(PASSAGE_TIMEOUT_MS);
	zassert_equal(lane.state, LANE_LOCKED);

	/* Feixe livre, mas o carro ainda perto: não é passagem. */
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 0;
	sim_dist[LANE_ENTRY] = MID;
	run(100);
	zassert_equal(sessions_count(), 0);

	/* Depois ele termina de passar: o carro está dentro, a sessão nasce. */
	sim_dist[LANE_ENTRY] = FAR;
	run(TICK_MS);
	zassert_true(sessions_find("ABC1D23", &entry_ms));
	zassert_equal(entry_ms, t);
	zassert_equal(lane.state, LANE_LOCKED, "segue travada até o reset");
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_ENABLE], 1, "não fecha");

	/* Só uma vez: o feixe de novo não mexe mais nas sessões. */
	sessions_clear();
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 1;
	run(100);
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 0;
	run(100);
	zassert_equal(sessions_count(), 0);
}

ZTEST(lane, test_late_passage_stops_on_beam_error)
{
	enter_until_green("ABC1D23");
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 1;
	run(PASSAGE_TIMEOUT_MS);
	zassert_equal(lane.state, LANE_LOCKED);
	fake_in[LANE_ENTRY][HW_IN_BEAM] = -EIO;
	run(100);
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 0;
	sim_dist[LANE_ENTRY] = FAR;
	run(100);
	zassert_equal(sessions_count(), 0, "leitura inválida encerra a observação");
}

ZTEST(lane, test_obstruction_during_close_locks_without_reversing)
{
	int enables;

	enter_until_green("ABC1D23");
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 1;
	run(100);
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 0;
	sim_dist[LANE_ENTRY] = FAR;
	run(TICK_MS);
	zassert_equal(lane.state, LANE_CLOSING);
	fake_in[LANE_ENTRY][HW_IN_UPPER] = 0;
	enables = fake_out_on_count[LANE_ENTRY][HW_OUT_ENABLE];

	fake_in[LANE_ENTRY][HW_IN_BEAM] = 1;
	lane_on_input(&lane);
	zassert_false(fake_out[LANE_ENTRY][HW_OUT_ENABLE], "parou na work");
	run(TICK_MS);
	zassert_equal(lane.state, LANE_LOCKED);
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 0;
	run(1000);
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_ENABLE], enables);
}

ZTEST(lane, test_beam_read_error_in_passage_locks)
{
	enter_until_green("ABC1D23");
	fake_in[LANE_ENTRY][HW_IN_BEAM] = -EIO;
	run(TICK_MS);
	zassert_equal(lane.state, LANE_LOCKED);
}

ZTEST(lane, test_open_error_locks)
{
	boot();
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	fake_out_err[LANE_ENTRY][HW_OUT_DIRECTION] = -EIO;
	reply_plate(1, "ABC1D23", 93);
	run(TICK_MS);
	zassert_equal(lane.state, LANE_LOCKED);
}

ZTEST(lane, test_close_error_locks)
{
	enter_until_green("ABC1D23");
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 1;
	run(100);
	fake_in[LANE_ENTRY][HW_IN_BEAM] = 0;
	sim_dist[LANE_ENTRY] = FAR;
	fake_out_err[LANE_ENTRY][HW_OUT_DIRECTION] = -EIO;
	run(TICK_MS);
	zassert_equal(lane.state, LANE_LOCKED);
}

/* ---------- pagamento na saída (REQ-05, 12) ---------- */

#define PLATE "ABC1D23"

static char charge[UART_FRAME_MAX + 1];

/* Carro com sessão chega à saída; termina em PAYING com a cobrança enviada. */
static void exit_until_paying(int64_t parked_ms)
{
	lane_init(&lane, LANE_EXIT);
	boot();
	zassert_ok(sessions_add(PLATE, t - parked_ms));
	sim_dist[LANE_EXIT] = NEAR;
	run(TICK_MS);
	reply_plate(lane.capture_id, PLATE, 93);
	run(TICK_MS);
	zassert_equal(lane.state, LANE_PAYING);
	snprintf(charge, sizeof(charge), "P1 CHARGE %s %s %u BRL\n", lane.uuid, PLATE,
		 (unsigned int)lane.amount);
}

static void reply_payment(const char *uuid, const char *result, uint32_t amount)
{
	char line[96];

	snprintf(line, sizeof(line), "P1 RESULT %s %s %u BRL\n", uuid, result,
		 (unsigned int)amount);
	fake_uart_rx(HW_UART_PAYMENT, line);
}

ZTEST(lane, test_exit_charge_is_drawn_frozen_and_sent)
{
	uint32_t expected;

	exit_until_paying(2500);
	zassert_ok(tariff_compute(2500 + TICK_MS, CONFIG_PARKING_TARIFF_UNIT_MS, &expected));
	zassert_equal(lane.amount, expected);
	zassert_true(payment_uuid_valid(lane.uuid));
	zassert_str_equal(fake_uart_tx[HW_UART_PAYMENT], charge);
	zassert_true(hw_display_write_fake.call_count > 0, "tela desenhada pela SPI");
	zassert_equal(fake_out_on_count[LANE_EXIT][HW_OUT_ENABLE], 0, "não abre antes");
	zassert_true(fake_out[LANE_EXIT][HW_OUT_RED]);
}

ZTEST(lane, test_exact_paid_opens)
{
	exit_until_paying(1000);
	reply_payment(lane.uuid, "PAID", lane.amount);
	run(TICK_MS);
	zassert_equal(lane.state, LANE_OPENING);
	zassert_equal(lane.uuid[0], '\0', "cobrança encerrada");
}

ZTEST(lane, test_wrong_paid_never_opens)
{
	char other[UUID_BUF];

	exit_until_paying(1000);
	payment_new_uuid(other, 1);
	reply_payment(other, "PAID", lane.amount);         /* UUID antigo/outro */
	reply_payment(lane.uuid, "PAID", lane.amount + 100); /* outro valor */
	fake_uart_rx(HW_UART_PAYMENT, "P1 ERROR TIMEOUT\n");
	fake_uart_rx(HW_UART_PAYMENT, "P1 RESULT broken\n");
	run(TICK_MS);
	zassert_equal(lane.state, LANE_PAYING);
	zassert_equal(fake_out_on_count[LANE_EXIT][HW_OUT_ENABLE], 0);
}

ZTEST(lane, test_retries_resend_the_same_line_then_cancel)
{
	char expected[3 * sizeof(charge) + 64];
	char cancel[64];

	exit_until_paying(1000);
	run(PAYMENT_TIMEOUT_MS);
	run(PAYMENT_TIMEOUT_MS);
	snprintf(expected, sizeof(expected), "%s%s%s", charge, charge, charge);
	zassert_str_equal(fake_uart_tx[HW_UART_PAYMENT], expected, "3 envios idênticos");
	zassert_equal(lane.state, LANE_PAYING);

	snprintf(cancel, sizeof(cancel), "P1 CANCEL %s\n", lane.uuid);
	run(PAYMENT_TIMEOUT_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	strcat(expected, cancel);
	zassert_str_equal(fake_uart_tx[HW_UART_PAYMENT], expected);
	zassert_equal(fake_out_on_count[LANE_EXIT][HW_OUT_BUZZER], 1);
}

ZTEST(lane, test_late_paid_after_cancel_does_not_open)
{
	char old[UUID_BUF];

	exit_until_paying(1000);
	memcpy(old, lane.uuid, sizeof(old));
	run(3 * PAYMENT_TIMEOUT_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	reply_payment(old, "PAID", lane.amount);
	run(100);
	zassert_equal(fake_out_on_count[LANE_EXIT][HW_OUT_ENABLE], 0);
}

ZTEST(lane, test_cancelled_requests_assistance)
{
	exit_until_paying(1000);
	reply_payment(lane.uuid, "CANCELLED", lane.amount);
	run(TICK_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(fake_out_on_count[LANE_EXIT][HW_OUT_ENABLE], 0);
}

ZTEST(lane, test_departure_during_payment_cancels)
{
	exit_until_paying(1000);
	sim_dist[LANE_EXIT] = FAR;
	run(TICK_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_not_null(strstr(fake_uart_tx[HW_UART_PAYMENT], "P1 CANCEL "));
}

ZTEST(lane, test_waits_without_fresh_distance)
{
	exit_until_paying(1000);
	adc_sampler_take_fake.custom_fake = NULL;
	run(500);
	zassert_equal(lane.state, LANE_PAYING);
}

ZTEST(lane, test_paid_exit_passage_removes_session)
{
	exit_until_paying(1000);
	reply_payment(lane.uuid, "PAID", lane.amount);
	run(TICK_MS);
	arm_at_top();
	run(TICK_MS);
	zassert_equal(lane.state, LANE_PASSAGE);
	zassert_equal(sessions_count(), 1, "pagar não encerra a sessão");
	fake_in[LANE_EXIT][HW_IN_BEAM] = 1;
	run(100);
	fake_in[LANE_EXIT][HW_IN_BEAM] = 0;
	sim_dist[LANE_EXIT] = FAR;
	run(TICK_MS);
	zassert_equal(sessions_count(), 0);
	zassert_equal(lane.state, LANE_CLOSING);

	/* Um PAID repetido depois não reabre nada. */
	arm_at_bottom();
	run(TICK_MS);
	reply_payment(lane.uuid, "PAID", lane.amount);
	run(100);
	zassert_equal(lane.state, LANE_IDLE);
}

ZTEST(lane, test_late_exit_passage_removes_session)
{
	exit_until_paying(1000);
	reply_payment(lane.uuid, "PAID", lane.amount);
	run(TICK_MS);
	arm_at_top();
	run(TICK_MS);
	zassert_equal(lane.state, LANE_PASSAGE);
	fake_in[LANE_EXIT][HW_IN_BEAM] = 1;
	run(PASSAGE_TIMEOUT_MS);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(sessions_count(), 1);
	fake_in[LANE_EXIT][HW_IN_BEAM] = 0;
	sim_dist[LANE_EXIT] = FAR;
	run(TICK_MS);
	zassert_equal(sessions_count(), 0, "o carro saiu de fato");
}

ZTEST(lane, test_tariff_overflow_requests_assistance)
{
	int64_t too_long = ((int64_t)TARIFF_MAX_UNITS + 1) * CONFIG_PARKING_TARIFF_UNIT_MS;

	lane_init(&lane, LANE_EXIT);
	boot();
	t = too_long + 1000;
	zassert_ok(sessions_add(PLATE, 0));
	sim_dist[LANE_EXIT] = NEAR;
	run(TICK_MS);
	reply_plate(lane.capture_id, PLATE, 93);
	run(TICK_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_str_equal(fake_uart_tx[HW_UART_PAYMENT], "", "não cobra");
}

/* Falha na n-ésima escrita SPI (0 = nunca). */
static unsigned int spi_writes;
static unsigned int spi_fail_at;

static int spi_custom(bool data, uint8_t *buf, size_t len)
{
	ARG_UNUSED(data);
	ARG_UNUSED(buf);
	ARG_UNUSED(len);
	spi_writes++;
	return spi_writes == spi_fail_at ? -EIO : 0;
}

static void exit_with_spi_failure(unsigned int fail_at)
{
	lane_init(&lane, LANE_EXIT);
	boot();
	spi_writes = 0;
	spi_fail_at = fail_at;
	hw_display_write_fake.custom_fake = spi_custom;
	zassert_ok(sessions_add(PLATE, 0));
	sim_dist[LANE_EXIT] = NEAR;
	run(TICK_MS);
	reply_plate(lane.capture_id, PLATE, 93);
	run(TICK_MS);
}

ZTEST(lane, test_display_failure_requests_assistance)
{
	exit_with_spi_failure(1); /* falha ao desenhar a placa */
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_str_equal(fake_uart_tx[HW_UART_PAYMENT], "");

	before(NULL);
	exit_with_spi_failure(7 * 6 + 1); /* falha ao desenhar o valor */
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_str_equal(fake_uart_tx[HW_UART_PAYMENT], "", "sem tela, sem cobrança");
}

ZTEST(lane, test_charge_write_error_locks)
{
	lane_init(&lane, LANE_EXIT);
	boot();
	zassert_ok(sessions_add(PLATE, 0));
	sim_dist[LANE_EXIT] = NEAR;
	run(TICK_MS);
	reply_plate(lane.capture_id, PLATE, 93);
	hw_uart_write_fake.return_val = -EIO;
	run(TICK_MS);
	zassert_equal(lane.state, LANE_LOCKED);
}

ZTEST(lane, test_second_assist_does_not_pulse_again)
{
	boot();
	sim_dist[LANE_ENTRY] = NEAR;
	run(TICK_MS);
	run(3 * CAMERA_TIMEOUT_MS + TICK_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	adc_sampler_failed_fake.return_val = true;
	run(TICK_MS);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(fake_out_on_count[LANE_ENTRY][HW_OUT_BUZZER], 1);
}
