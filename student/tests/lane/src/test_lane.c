/**
 * @file test_lane.c
 * @brief Testes da máquina de estados das faixas (lane.c).
 *
 * O hardware, o ADC, o pagamento, a tarifa e o display são falsos do FFF que
 * leem um "mundo" simulado aqui; câmera, sessões e montador de quadros são os
 * módulos reais.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "display.h"
#include "distance.h"
#include "lane.h"
#include "payment.h"
#include "session.h"
#include "tariff.h"

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, hw_output_set, enum hw_lane, enum hw_output, bool);
FAKE_VALUE_FUNC(int, hw_input_get, enum hw_lane, enum hw_input);
FAKE_VALUE_FUNC(int, hw_uart_read, enum hw_uart, uint8_t *);
FAKE_VALUE_FUNC(int, hw_uart_write, enum hw_uart, const uint8_t *, size_t);
FAKE_VALUE_FUNC(int, distance_get, enum hw_lane, int64_t, uint16_t *);
FAKE_VALUE_FUNC(bool, distance_faulted);
FAKE_VALUE_FUNC(enum payment_event, payment_poll, int64_t, bool);
FAKE_VALUE_FUNC(int, payment_start, const char *, uint32_t, int64_t);
FAKE_VALUE_FUNC(int, payment_abort);
FAKE_VALUE_FUNC(int, display_show_plate, const char *);
FAKE_VALUE_FUNC(int, display_show_charge, uint32_t);
FAKE_VALUE_FUNC(int, display_status);
FAKE_VALUE_FUNC(int, tariff_compute, int64_t, uint32_t, uint32_t *);

#define PLATE "ABC1D23"
#define NEAR 500
#define FAR 2000
#define AMOUNT 700

/** @brief Estado do mundo simulado lido e escrito pelos falsos. */
static struct {
	int64_t now;
	bool out[HW_LANE_COUNT][HW_OUT_COUNT];
	int in[HW_LANE_COUNT][HW_IN_COUNT];
	int in_err[HW_IN_COUNT];
	bool beam_when_green_off; /**< Ativa o feixe ao apagar o verde. */
	int fail_output; /**< Saída cuja escrita falha, ou -1. */
	unsigned int buzzes[HW_LANE_COUNT];
	unsigned int enables[HW_LANE_COUNT];
	uint16_t dist[HW_LANE_COUNT];
	int dist_err;
	const char *rx[HW_UART_COUNT];
	char tx[HW_UART_COUNT][128];
	int tx_err;
} w;

static int fake_output_set(enum hw_lane lane, enum hw_output output, bool on)
{
	if ((int)output == w.fail_output) {
		return -EIO;
	}
	if (output == HW_OUT_BUZZER && on && !w.out[lane][output]) {
		w.buzzes[lane]++;
	}
	if (output == HW_OUT_MOTOR_ENABLE && on) {
		w.enables[lane]++;
	}
	if (output == HW_OUT_GREEN && !on && w.beam_when_green_off) {
		w.in[lane][HW_IN_BEAM] = 1;
	}
	w.out[lane][output] = on;
	return 0;
}

static int fake_input_get(enum hw_lane lane, enum hw_input input)
{
	return w.in_err[input] != 0 ? w.in_err[input] : w.in[lane][input];
}

static int fake_uart_read(enum hw_uart uart, uint8_t *byte)
{
	if (w.rx[uart] == NULL || *w.rx[uart] == '\0') {
		return -EAGAIN;
	}
	*byte = (uint8_t)*w.rx[uart]++;
	return 0;
}

static int fake_uart_write(enum hw_uart uart, const uint8_t *data, size_t len)
{
	if (w.tx_err != 0) {
		return w.tx_err;
	}
	strncat(w.tx[uart], (const char *)data, len);
	return 0;
}

static int fake_distance_get(enum hw_lane lane, int64_t now_ms, uint16_t *raw)
{
	ARG_UNUSED(now_ms);
	*raw = w.dist[lane];
	return w.dist_err;
}

static int fake_tariff(int64_t elapsed_ms, uint32_t unit_ms, uint32_t *amount)
{
	ARG_UNUSED(elapsed_ms);
	ARG_UNUSED(unit_ms);
	*amount = AMOUNT;
	return 0;
}

/** @brief Remove uma sessão se ela existir. */
static void forget(const char *plate)
{
	int err = session_remove(plate);

	zassert_true(err == 0 || err == -ENOENT);
}

static void reset_world(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(hw_output_set);
	RESET_FAKE(hw_input_get);
	RESET_FAKE(hw_uart_read);
	RESET_FAKE(hw_uart_write);
	RESET_FAKE(distance_get);
	RESET_FAKE(distance_faulted);
	RESET_FAKE(payment_poll);
	RESET_FAKE(payment_start);
	RESET_FAKE(payment_abort);
	RESET_FAKE(display_show_plate);
	RESET_FAKE(display_show_charge);
	RESET_FAKE(display_status);
	RESET_FAKE(tariff_compute);
	FFF_RESET_HISTORY();
	hw_output_set_fake.custom_fake = fake_output_set;
	hw_input_get_fake.custom_fake = fake_input_get;
	hw_uart_read_fake.custom_fake = fake_uart_read;
	hw_uart_write_fake.custom_fake = fake_uart_write;
	distance_get_fake.custom_fake = fake_distance_get;
	tariff_compute_fake.custom_fake = fake_tariff;

	memset(&w, 0, sizeof(w));
	w.now = 1000;
	w.fail_output = -1;
	for (int lane = 0; lane < HW_LANE_COUNT; lane++) {
		w.out[lane][HW_OUT_RED] = true;
		w.in[lane][HW_IN_LIMIT_LOWER] = 1;
		w.dist[lane] = FAR;
	}
	forget(PLATE);
}

ZTEST_SUITE(lane, NULL, NULL, reset_world, NULL, NULL);

/** @brief Avança o tempo, atendendo a faixa a cada 1 ms. */
static void run(enum hw_lane lane, int ms)
{
	for (int i = 0; i < ms; i++) {
		w.now++;
		lane_step(lane, w.now);
	}
}

/** @brief Atende a faixa uma vez, sem avançar o tempo. */
static void step(enum hw_lane lane)
{
	lane_step(lane, w.now);
}

static enum lane_state state(enum hw_lane lane)
{
	return lane_state_get(lane);
}

static enum hw_uart camera_of(enum hw_lane lane)
{
	return lane == HW_LANE_ENTRY ? HW_UART_CAMERA_ENTRY : HW_UART_CAMERA_EXIT;
}

/** @brief Inicia a faixa e espera os sensores assentarem. */
static void boot(enum hw_lane lane)
{
	lane_init(lane, w.now);
	run(lane, LANE_SETTLE_MS);
	zassert_equal(state(lane), LANE_IDLE);
}

/** @brief Aproxima um veículo e confere o primeiro pedido de captura. */
static void arrive(enum hw_lane lane)
{
	w.dist[lane] = NEAR;
	step(lane);
	zassert_equal(state(lane), LANE_IDENTIFYING);
	zassert_str_equal(w.tx[camera_of(lane)], "AT+CAPTURE=1\r\n");
}

/** @brief Entrega bytes à câmera da faixa e atende a faixa. */
static void reply(enum hw_lane lane, const char *bytes)
{
	w.rx[camera_of(lane)] = bytes;
	step(lane);
}

/** @brief Leva a entrada até o motor abrindo. */
static void to_opening(void)
{
	boot(HW_LANE_ENTRY);
	arrive(HW_LANE_ENTRY);
	reply(HW_LANE_ENTRY, "+PLATE:1,\"" PLATE "\",93\r\n");
	zassert_equal(state(HW_LANE_ENTRY), LANE_OPENING);
	zassert_true(w.out[HW_LANE_ENTRY][HW_OUT_MOTOR_DIRECTION]);
	zassert_true(w.out[HW_LANE_ENTRY][HW_OUT_MOTOR_ENABLE]);
	zassert_true(w.out[HW_LANE_ENTRY][HW_OUT_RED]);
	zassert_false(w.out[HW_LANE_ENTRY][HW_OUT_GREEN]);
}

/** @brief Leva a faixa do motor abrindo até a cancela aberta. */
static void reach_upper(enum hw_lane lane)
{
	w.in[lane][HW_IN_LIMIT_LOWER] = 0;
	run(lane, 1000);
	zassert_equal(state(lane), LANE_OPENING);
	w.in[lane][HW_IN_LIMIT_UPPER] = 1;
	step(lane);
	zassert_equal(state(lane), LANE_OPEN);
	zassert_false(w.out[lane][HW_OUT_MOTOR_ENABLE]);
	zassert_true(w.out[lane][HW_OUT_GREEN]);
	zassert_false(w.out[lane][HW_OUT_RED]);
}

/** @brief Faz o veículo cortar o feixe e se afastar, iniciando o fechamento. */
static void pass_through(enum hw_lane lane)
{
	w.in[lane][HW_IN_BEAM] = 1;
	run(lane, 500);
	zassert_equal(state(lane), LANE_PASSING);
	w.in[lane][HW_IN_BEAM] = 0;
	w.dist[lane] = FAR;
	step(lane);
	zassert_equal(state(lane), LANE_CLOSING);
	zassert_false(w.out[lane][HW_OUT_GREEN]);
	zassert_true(w.out[lane][HW_OUT_RED]);
	zassert_false(w.out[lane][HW_OUT_MOTOR_DIRECTION]);
	zassert_true(w.out[lane][HW_OUT_MOTOR_ENABLE]);
}

/** @brief Confere a assistência: motor parado, vermelho e um só pulso do buzzer. */
static void assert_assistance(enum hw_lane lane, enum lane_state expected)
{
	zassert_equal(state(lane), expected);
	zassert_false(w.out[lane][HW_OUT_MOTOR_ENABLE]);
	zassert_false(w.out[lane][HW_OUT_GREEN]);
	zassert_true(w.out[lane][HW_OUT_RED]);
	zassert_equal(w.buzzes[lane], 1);
}

/** @brief Esgota as três capturas por prazo vencido. */
static void exhaust_captures(enum hw_lane lane)
{
	arrive(lane);
	run(lane, LANE_CAPTURE_TIMEOUT_MS);
	zassert_str_equal(w.tx[camera_of(lane)], "AT+CAPTURE=1\r\nAT+CAPTURE=2\r\n");
	run(lane, 2 * LANE_CAPTURE_TIMEOUT_MS);
	assert_assistance(lane, LANE_ASSIST);
}

ZTEST(lane, test_boot_waits_for_sensors)
{
	w.in[HW_LANE_ENTRY][HW_IN_LIMIT_LOWER] = 0;
	lane_init(HW_LANE_ENTRY, w.now);
	run(HW_LANE_ENTRY, LANE_SETTLE_MS - 1);
	zassert_equal(state(HW_LANE_ENTRY), LANE_BOOT);
	zassert_equal(w.buzzes[HW_LANE_ENTRY], 0);
	run(HW_LANE_ENTRY, 1);
	zassert_equal(state(HW_LANE_ENTRY), LANE_IDLE);
}

ZTEST(lane, test_idle_without_lower_limit_latches_without_homing)
{
	boot(HW_LANE_ENTRY);
	w.in[HW_LANE_ENTRY][HW_IN_LIMIT_LOWER] = 0;
	w.dist[HW_LANE_ENTRY] = NEAR;
	step(HW_LANE_ENTRY);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
	zassert_true(w.out[HW_LANE_ENTRY][HW_OUT_BUZZER]);
	run(HW_LANE_ENTRY, LANE_BUZZER_PULSE_MS - 1);
	zassert_true(w.out[HW_LANE_ENTRY][HW_OUT_BUZZER]);
	run(HW_LANE_ENTRY, 1);
	zassert_false(w.out[HW_LANE_ENTRY][HW_OUT_BUZZER]);
	zassert_str_equal(w.tx[HW_UART_CAMERA_ENTRY], "");
	zassert_false(w.out[HW_LANE_ENTRY][HW_OUT_MOTOR_DIRECTION]);
}

ZTEST(lane, test_contradictory_limits_stay_latched)
{
	boot(HW_LANE_ENTRY);
	w.in[HW_LANE_ENTRY][HW_IN_LIMIT_UPPER] = 1;
	step(HW_LANE_ENTRY);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
	w.in[HW_LANE_ENTRY][HW_IN_LIMIT_UPPER] = 0;
	run(HW_LANE_ENTRY, 3000);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
}

ZTEST(lane, test_input_read_errors_latch)
{
	for (int input = 0; input < HW_IN_COUNT; input++) {
		boot(HW_LANE_ENTRY);
		w.in_err[input] = -EIO;
		step(HW_LANE_ENTRY);
		zassert_equal(state(HW_LANE_ENTRY), LANE_FAULT, "entrada %d", input);
		w.in_err[input] = 0;
	}
}

ZTEST(lane, test_adc_fault_latches)
{
	boot(HW_LANE_ENTRY);
	distance_faulted_fake.return_val = true;
	step(HW_LANE_ENTRY);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
}

ZTEST(lane, test_entry_full_cycle_creates_session)
{
	int64_t entry_ms;

	to_opening();
	reach_upper(HW_LANE_ENTRY);
	pass_through(HW_LANE_ENTRY);
	zassert_ok(session_find(PLATE, &entry_ms));
	zassert_equal(entry_ms, w.now);

	run(HW_LANE_ENTRY, 1000);
	zassert_equal(state(HW_LANE_ENTRY), LANE_CLOSING);
	w.in[HW_LANE_ENTRY][HW_IN_LIMIT_UPPER] = 0;
	w.in[HW_LANE_ENTRY][HW_IN_LIMIT_LOWER] = 1;
	step(HW_LANE_ENTRY);
	zassert_equal(state(HW_LANE_ENTRY), LANE_IDLE);
	zassert_false(w.out[HW_LANE_ENTRY][HW_OUT_MOTOR_ENABLE]);
	zassert_true(w.out[HW_LANE_ENTRY][HW_OUT_RED]);
	zassert_equal(w.buzzes[HW_LANE_ENTRY], 0);
}

ZTEST(lane, test_low_confidence_retries_with_fresh_ids)
{
	boot(HW_LANE_ENTRY);
	arrive(HW_LANE_ENTRY);
	reply(HW_LANE_ENTRY, "+PLATE:1,\"" PLATE "\",62\r\n");
	reply(HW_LANE_ENTRY, "+PLATE:2,\"" PLATE "\",79\r\n");
	zassert_equal(state(HW_LANE_ENTRY), LANE_IDENTIFYING);
	reply(HW_LANE_ENTRY, "+PLATE:3,\"" PLATE "\",84\r\n");
	zassert_str_equal(w.tx[HW_UART_CAMERA_ENTRY],
			  "AT+CAPTURE=1\r\nAT+CAPTURE=2\r\nAT+CAPTURE=3\r\n");
	assert_assistance(HW_LANE_ENTRY, LANE_ASSIST);
}

ZTEST(lane, test_confidence_85_is_accepted)
{
	boot(HW_LANE_ENTRY);
	arrive(HW_LANE_ENTRY);
	reply(HW_LANE_ENTRY, "+PLATE:1,\"" PLATE "\",85\r\n");
	zassert_equal(state(HW_LANE_ENTRY), LANE_OPENING);
}

ZTEST(lane, test_timeouts_consume_attempts)
{
	boot(HW_LANE_ENTRY);
	exhaust_captures(HW_LANE_ENTRY);
	run(HW_LANE_ENTRY, 5000);
	zassert_str_equal(w.tx[HW_UART_CAMERA_ENTRY],
			  "AT+CAPTURE=1\r\nAT+CAPTURE=2\r\nAT+CAPTURE=3\r\n");
}

ZTEST(lane, test_other_frames_do_not_authorize)
{
	boot(HW_LANE_ENTRY);
	arrive(HW_LANE_ENTRY);
	reply(HW_LANE_ENTRY, "OK\r\n");
	reply(HW_LANE_ENTRY, "+PLATE:9,\"" PLATE "\",93\r\n");
	reply(HW_LANE_ENTRY, "ERROR\r\n");
	zassert_equal(state(HW_LANE_ENTRY), LANE_IDENTIFYING);
	zassert_str_equal(w.tx[HW_UART_CAMERA_ENTRY], "AT+CAPTURE=1\r\n");
}

ZTEST(lane, test_result_needs_fresh_distance)
{
	boot(HW_LANE_ENTRY);
	arrive(HW_LANE_ENTRY);
	w.dist_err = -ENODATA;
	reply(HW_LANE_ENTRY, "+PLATE:1,\"" PLATE "\",93\r\n");
	zassert_equal(state(HW_LANE_ENTRY), LANE_IDENTIFYING);
}

ZTEST(lane, test_stale_distance_does_not_start_identification)
{
	boot(HW_LANE_ENTRY);
	w.dist[HW_LANE_ENTRY] = NEAR;
	w.dist_err = -ENODATA;
	run(HW_LANE_ENTRY, 100);
	zassert_equal(state(HW_LANE_ENTRY), LANE_IDLE);
	zassert_str_equal(w.tx[HW_UART_CAMERA_ENTRY], "");
}

ZTEST(lane, test_departure_abandons_identification)
{
	boot(HW_LANE_ENTRY);
	arrive(HW_LANE_ENTRY);
	w.dist[HW_LANE_ENTRY] = FAR;
	step(HW_LANE_ENTRY);
	zassert_equal(state(HW_LANE_ENTRY), LANE_IDLE);
	zassert_equal(w.buzzes[HW_LANE_ENTRY], 0);
}

ZTEST(lane, test_assistance_rearms_after_departure_with_clear_beam)
{
	boot(HW_LANE_ENTRY);
	exhaust_captures(HW_LANE_ENTRY);
	w.dist[HW_LANE_ENTRY] = FAR;
	w.in[HW_LANE_ENTRY][HW_IN_BEAM] = 1;
	step(HW_LANE_ENTRY);
	zassert_equal(state(HW_LANE_ENTRY), LANE_ASSIST);
	w.in[HW_LANE_ENTRY][HW_IN_BEAM] = 0;
	step(HW_LANE_ENTRY);
	zassert_equal(state(HW_LANE_ENTRY), LANE_IDLE);
}

ZTEST(lane, test_assistance_without_lower_limit_latches)
{
	boot(HW_LANE_ENTRY);
	exhaust_captures(HW_LANE_ENTRY);
	w.in[HW_LANE_ENTRY][HW_IN_LIMIT_LOWER] = 0;
	step(HW_LANE_ENTRY);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
}

ZTEST(lane, test_parked_plate_cannot_enter_again)
{
	zassert_ok(session_add(PLATE, 0));
	boot(HW_LANE_ENTRY);
	arrive(HW_LANE_ENTRY);
	reply(HW_LANE_ENTRY, "+PLATE:1,\"" PLATE "\",93\r\n");
	assert_assistance(HW_LANE_ENTRY, LANE_ASSIST);
}

ZTEST(lane, test_full_lot_refuses_entry)
{
	static const char *const others[] = {"AAA0000", "AAA0001", "AAA0002", "AAA0003",
					     "AAA0004", "AAA0005", "AAA0006", "AAA0007"};

	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		zassert_ok(session_add(others[i], 0));
	}
	boot(HW_LANE_ENTRY);
	arrive(HW_LANE_ENTRY);
	reply(HW_LANE_ENTRY, "+PLATE:1,\"" PLATE "\",93\r\n");
	assert_assistance(HW_LANE_ENTRY, LANE_ASSIST);
	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		zassert_ok(session_remove(others[i]));
	}
}

ZTEST(lane, test_opening_budget_latches)
{
	to_opening();
	run(HW_LANE_ENTRY, LANE_MOTOR_BUDGET_MS - 1);
	zassert_equal(state(HW_LANE_ENTRY), LANE_OPENING);
	run(HW_LANE_ENTRY, 1);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
}

ZTEST(lane, test_passage_budget_counts_while_beam_is_held)
{
	to_opening();
	reach_upper(HW_LANE_ENTRY);
	w.in[HW_LANE_ENTRY][HW_IN_BEAM] = 1;
	run(HW_LANE_ENTRY, LANE_PASSAGE_BUDGET_MS - 1);
	zassert_equal(state(HW_LANE_ENTRY), LANE_PASSING);
	run(HW_LANE_ENTRY, 1);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
	zassert_equal(session_find(PLATE, NULL), -ENOENT);
}

ZTEST(lane, test_passage_needs_beam_then_departure)
{
	to_opening();
	reach_upper(HW_LANE_ENTRY);
	w.dist[HW_LANE_ENTRY] = FAR;
	run(HW_LANE_ENTRY, 100);
	zassert_equal(state(HW_LANE_ENTRY), LANE_OPEN);
	w.dist[HW_LANE_ENTRY] = NEAR;
	w.in[HW_LANE_ENTRY][HW_IN_BEAM] = 1;
	step(HW_LANE_ENTRY);
	w.in[HW_LANE_ENTRY][HW_IN_BEAM] = 0;
	run(HW_LANE_ENTRY, 100);
	zassert_equal(state(HW_LANE_ENTRY), LANE_PASSING);
	w.dist[HW_LANE_ENTRY] = FAR;
	step(HW_LANE_ENTRY);
	zassert_equal(state(HW_LANE_ENTRY), LANE_CLOSING);
}

ZTEST(lane, test_obstruction_while_closing_stops_without_reversal)
{
	unsigned int enables;

	to_opening();
	reach_upper(HW_LANE_ENTRY);
	pass_through(HW_LANE_ENTRY);
	run(HW_LANE_ENTRY, 500);
	w.in[HW_LANE_ENTRY][HW_IN_BEAM] = 1;
	step(HW_LANE_ENTRY);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
	enables = w.enables[HW_LANE_ENTRY];
	w.in[HW_LANE_ENTRY][HW_IN_BEAM] = 0;
	run(HW_LANE_ENTRY, 5000);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
	zassert_equal(w.enables[HW_LANE_ENTRY], enables);
}

ZTEST(lane, test_closing_budget_latches)
{
	to_opening();
	reach_upper(HW_LANE_ENTRY);
	pass_through(HW_LANE_ENTRY);
	run(HW_LANE_ENTRY, LANE_MOTOR_BUDGET_MS - 1);
	zassert_equal(state(HW_LANE_ENTRY), LANE_CLOSING);
	run(HW_LANE_ENTRY, 1);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
}

ZTEST(lane, test_exit_without_session_assists_without_charging)
{
	boot(HW_LANE_EXIT);
	arrive(HW_LANE_EXIT);
	reply(HW_LANE_EXIT, "+PLATE:1,\"" PLATE "\",93\r\n");
	assert_assistance(HW_LANE_EXIT, LANE_ASSIST);
	zassert_equal(payment_start_fake.call_count, 0);
	zassert_equal(display_show_plate_fake.call_count, 0);
}

/** @brief Começa a preparar a cobrança de um veículo estacionado. */
static void start_display(void)
{
	memset(w.tx, 0, sizeof(w.tx));
	zassert_ok(session_add(PLATE, w.now));
	boot(HW_LANE_EXIT);
	arrive(HW_LANE_EXIT);
	FFF_RESET_HISTORY();
	reply(HW_LANE_EXIT, "+PLATE:1,\"" PLATE "\",93\r\n");
}

/** @brief Entrega ao controle as duas conclusões do display falso. */
static void to_paying(void)
{
	start_display();
	step(HW_LANE_EXIT);
	step(HW_LANE_EXIT);
}

/** @brief Posição da primeira chamada a um falso no histórico do FFF, ou -1. */
static int called_at(void *fake)
{
	for (unsigned int i = 0; i < fff.call_history_idx; i++) {
		if (fff.call_history[i] == fake) {
			return (int)i;
		}
	}
	return -1;
}

ZTEST(lane, test_exit_freezes_charge_between_plate_and_amount)
{
	start_display();
	zassert_equal(state(HW_LANE_EXIT), LANE_SHOWING_PLATE);
	display_status_fake.return_val = -EAGAIN;
	run(HW_LANE_EXIT, 25);
	zassert_equal(tariff_compute_fake.call_count, 0);
	zassert_equal(display_show_charge_fake.call_count, 0);
	display_status_fake.return_val = 0;
	step(HW_LANE_EXIT);
	zassert_equal(state(HW_LANE_EXIT), LANE_SHOWING_CHARGE);
	display_status_fake.return_val = -EAGAIN;
	run(HW_LANE_EXIT, 25);
	zassert_equal(payment_start_fake.call_count, 0);
	display_status_fake.return_val = 0;
	step(HW_LANE_EXIT);
	zassert_equal(state(HW_LANE_EXIT), LANE_PAYING);
	zassert_equal(display_show_plate_fake.call_count, 1);
	zassert_equal(tariff_compute_fake.call_count, 1);
	zassert_equal(display_show_charge_fake.call_count, 1);
	zassert_equal(payment_start_fake.call_count, 1);
	zassert_str_equal(display_show_plate_fake.arg0_val, PLATE);
	zassert_equal(tariff_compute_fake.arg0_val, LANE_SETTLE_MS + 25);
	zassert_equal(tariff_compute_fake.arg1_val, CONFIG_PARKING_TARIFF_UNIT_MS);
	zassert_equal(display_show_charge_fake.arg0_val, AMOUNT);
	zassert_str_equal(payment_start_fake.arg0_val, PLATE);
	zassert_equal(payment_start_fake.arg1_val, AMOUNT);
}

ZTEST(lane, test_exit_opens_only_after_paid_and_passage_ends_session)
{
	to_paying();
	run(HW_LANE_EXIT, 500);
	zassert_equal(state(HW_LANE_EXIT), LANE_PAYING);
	zassert_false(w.out[HW_LANE_EXIT][HW_OUT_MOTOR_ENABLE]);
	payment_poll_fake.return_val = PAYMENT_EV_PAID;
	step(HW_LANE_EXIT);
	payment_poll_fake.return_val = PAYMENT_EV_NONE;
	zassert_equal(state(HW_LANE_EXIT), LANE_OPENING);
	reach_upper(HW_LANE_EXIT);
	zassert_ok(session_find(PLATE, NULL));
	pass_through(HW_LANE_EXIT);
	zassert_equal(session_find(PLATE, NULL), -ENOENT);
}

ZTEST(lane, test_exit_payment_failure_assists)
{
	to_paying();
	payment_poll_fake.return_val = PAYMENT_EV_FAILED;
	step(HW_LANE_EXIT);
	assert_assistance(HW_LANE_EXIT, LANE_ASSIST);
}

ZTEST(lane, test_exit_departure_cancels_payment)
{
	to_paying();
	w.dist[HW_LANE_EXIT] = FAR;
	step(HW_LANE_EXIT);
	assert_assistance(HW_LANE_EXIT, LANE_ASSIST);
	zassert_equal(payment_abort_fake.call_count, 1);
}

ZTEST(lane, test_exit_charge_errors_assist)
{
	int *const rets[] = {
		&display_show_plate_fake.return_val,
		&display_show_charge_fake.return_val,
		&payment_start_fake.return_val,
	};

	for (size_t i = 0; i < ARRAY_SIZE(rets); i++) {
		*rets[i] = -ENOMEM;
		to_paying();
		zassert_equal(state(HW_LANE_EXIT), LANE_ASSIST, "erro %u", (unsigned int)i);
		*rets[i] = 0;
		forget(PLATE);
	}
	tariff_compute_fake.custom_fake = NULL;
	tariff_compute_fake.return_val = -ERANGE;
	to_paying();
	zassert_equal(state(HW_LANE_EXIT), LANE_ASSIST);
	zassert_equal(payment_start_fake.call_count, 1);
}

ZTEST(lane, test_payment_abort_error_latches)
{
	to_paying();
	payment_abort_fake.return_val = -EIO;
	w.dist[HW_LANE_EXIT] = FAR;
	step(HW_LANE_EXIT);
	zassert_equal(state(HW_LANE_EXIT), LANE_FAULT);
}

ZTEST(lane, test_camera_write_error_assists)
{
	boot(HW_LANE_ENTRY);
	w.tx_err = -EIO;
	w.dist[HW_LANE_ENTRY] = NEAR;
	step(HW_LANE_ENTRY);
	assert_assistance(HW_LANE_ENTRY, LANE_ASSIST);
}

ZTEST(lane, test_open_drive_errors_latch)
{
	static const int outs[] = {HW_OUT_MOTOR_DIRECTION, HW_OUT_MOTOR_ENABLE};

	for (size_t i = 0; i < ARRAY_SIZE(outs); i++) {
		boot(HW_LANE_ENTRY);
		arrive(HW_LANE_ENTRY);
		w.fail_output = outs[i];
		reply(HW_LANE_ENTRY, "+PLATE:1,\"" PLATE "\",93\r\n");
		zassert_equal(state(HW_LANE_ENTRY), LANE_FAULT, "saída %d", outs[i]);
		w.fail_output = -1;
		memset(w.tx, 0, sizeof(w.tx));
	}
}

ZTEST(lane, test_upper_limit_drive_errors_latch)
{
	static const int outs[] = {HW_OUT_MOTOR_ENABLE, HW_OUT_RED, HW_OUT_GREEN};

	for (size_t i = 0; i < ARRAY_SIZE(outs); i++) {
		memset(&w.in, 0, sizeof(w.in));
		w.in[HW_LANE_ENTRY][HW_IN_LIMIT_LOWER] = 1;
		memset(w.tx, 0, sizeof(w.tx));
		to_opening();
		w.in[HW_LANE_ENTRY][HW_IN_LIMIT_LOWER] = 0;
		w.in[HW_LANE_ENTRY][HW_IN_LIMIT_UPPER] = 1;
		w.fail_output = outs[i];
		step(HW_LANE_ENTRY);
		zassert_equal(state(HW_LANE_ENTRY), LANE_FAULT, "saída %d", outs[i]);
		w.fail_output = -1;
	}
}

ZTEST(lane, test_passage_errors_latch)
{
	static const int outs[] = {HW_OUT_GREEN, HW_OUT_RED, HW_OUT_MOTOR_DIRECTION,
				   HW_OUT_MOTOR_ENABLE};

	for (size_t i = 0; i < ARRAY_SIZE(outs); i++) {
		memset(&w.in, 0, sizeof(w.in));
		w.in[HW_LANE_ENTRY][HW_IN_LIMIT_LOWER] = 1;
		memset(w.tx, 0, sizeof(w.tx));
		memset(&w.out, 0, sizeof(w.out));
		w.out[HW_LANE_ENTRY][HW_OUT_RED] = true;
		to_opening();
		reach_upper(HW_LANE_ENTRY);
		w.in[HW_LANE_ENTRY][HW_IN_BEAM] = 1;
		step(HW_LANE_ENTRY);
		w.in[HW_LANE_ENTRY][HW_IN_BEAM] = 0;
		w.dist[HW_LANE_ENTRY] = FAR;
		w.fail_output = outs[i];
		step(HW_LANE_ENTRY);
		zassert_equal(state(HW_LANE_ENTRY), LANE_FAULT, "saída %d", outs[i]);
		w.fail_output = -1;
		forget(PLATE);
	}
}

ZTEST(lane, test_session_error_at_passage_latches)
{
	to_opening();
	reach_upper(HW_LANE_ENTRY);
	zassert_ok(session_add(PLATE, 0));
	w.in[HW_LANE_ENTRY][HW_IN_BEAM] = 1;
	step(HW_LANE_ENTRY);
	w.in[HW_LANE_ENTRY][HW_IN_BEAM] = 0;
	w.dist[HW_LANE_ENTRY] = FAR;
	step(HW_LANE_ENTRY);
	zassert_equal(state(HW_LANE_ENTRY), LANE_FAULT);
}

ZTEST(lane, test_closing_stop_error_latches)
{
	to_opening();
	reach_upper(HW_LANE_ENTRY);
	pass_through(HW_LANE_ENTRY);
	w.in[HW_LANE_ENTRY][HW_IN_LIMIT_UPPER] = 0;
	w.in[HW_LANE_ENTRY][HW_IN_LIMIT_LOWER] = 1;
	w.fail_output = HW_OUT_MOTOR_ENABLE;
	step(HW_LANE_ENTRY);
	zassert_equal(state(HW_LANE_ENTRY), LANE_FAULT);
}

ZTEST(lane, test_assistance_drive_errors_latch)
{
	static const int outs[] = {HW_OUT_MOTOR_ENABLE, HW_OUT_GREEN, HW_OUT_RED};

	for (size_t i = 0; i < ARRAY_SIZE(outs); i++) {
		boot(HW_LANE_ENTRY);
		w.fail_output = outs[i];
		w.tx_err = -EIO;
		w.dist[HW_LANE_ENTRY] = NEAR;
		step(HW_LANE_ENTRY);
		zassert_equal(state(HW_LANE_ENTRY), LANE_FAULT, "saída %d", outs[i]);
		w.fail_output = -1;
		w.tx_err = 0;
		w.dist[HW_LANE_ENTRY] = FAR;
	}
}

ZTEST(lane, test_buzzer_errors_latch_on_next_step)
{
	boot(HW_LANE_ENTRY);
	w.fail_output = HW_OUT_BUZZER;
	w.tx_err = -EIO;
	w.dist[HW_LANE_ENTRY] = NEAR;
	step(HW_LANE_ENTRY);
	zassert_equal(state(HW_LANE_ENTRY), LANE_ASSIST);
	zassert_equal(w.buzzes[HW_LANE_ENTRY], 0);
	step(HW_LANE_ENTRY);
	zassert_equal(state(HW_LANE_ENTRY), LANE_FAULT);

	w.fail_output = -1;
	boot(HW_LANE_ENTRY);
	step(HW_LANE_ENTRY);
	zassert_equal(state(HW_LANE_ENTRY), LANE_ASSIST);
	w.fail_output = HW_OUT_BUZZER;
	run(HW_LANE_ENTRY, LANE_BUZZER_PULSE_MS + 1);
	zassert_equal(state(HW_LANE_ENTRY), LANE_FAULT);
	zassert_true(w.out[HW_LANE_ENTRY][HW_OUT_BUZZER]);
}

ZTEST(lane, test_lane_fail_latches_and_cancels_payment)
{
	lane_init(HW_LANE_EXIT, w.now);
	lane_fail(HW_LANE_EXIT, w.now);
	assert_assistance(HW_LANE_EXIT, LANE_FAULT);
	zassert_equal(payment_abort_fake.call_count, 1);
}

ZTEST(lane, test_boot_latches_transient_contradiction)
{
	lane_init(HW_LANE_ENTRY, w.now);
	w.in[HW_LANE_ENTRY][HW_IN_LIMIT_UPPER] = 1;
	step(HW_LANE_ENTRY);
	w.in[HW_LANE_ENTRY][HW_IN_LIMIT_UPPER] = 0;
	w.dist[HW_LANE_ENTRY] = NEAR;
	run(HW_LANE_ENTRY, LANE_SETTLE_MS + 1);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
	zassert_equal(w.enables[HW_LANE_ENTRY], 0);
}

ZTEST(lane, test_lost_lower_limit_during_capture_prevents_opening)
{
	boot(HW_LANE_ENTRY);
	arrive(HW_LANE_ENTRY);
	w.in[HW_LANE_ENTRY][HW_IN_LIMIT_LOWER] = 0;
	reply(HW_LANE_ENTRY, "+PLATE:1,\"" PLATE "\",93\r\n");
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
	zassert_equal(w.enables[HW_LANE_ENTRY], 0);
}

ZTEST(lane, test_lost_upper_limit_turns_green_off)
{
	to_opening();
	reach_upper(HW_LANE_ENTRY);
	w.in[HW_LANE_ENTRY][HW_IN_LIMIT_UPPER] = 0;
	step(HW_LANE_ENTRY);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
}

ZTEST(lane, test_one_noisy_read_does_not_start_capture)
{
	boot(HW_LANE_ENTRY);
	w.dist[HW_LANE_ENTRY] = 668;
	step(HW_LANE_ENTRY);
	w.dist[HW_LANE_ENTRY] = 716;
	run(HW_LANE_ENTRY, 200);
	zassert_equal(state(HW_LANE_ENTRY), LANE_IDLE);
	zassert_str_equal(w.tx[HW_UART_CAMERA_ENTRY], "");
	w.dist[HW_LANE_ENTRY] = DISTANCE_ARRIVAL_CODE - 1;
	run(HW_LANE_ENTRY, 340);
	zassert_equal(state(HW_LANE_ENTRY), LANE_IDENTIFYING);
}

ZTEST(lane, test_late_plate_starts_next_capture_without_opening)
{
	boot(HW_LANE_ENTRY);
	arrive(HW_LANE_ENTRY);
	w.now += LANE_CAPTURE_TIMEOUT_MS + 1;
	reply(HW_LANE_ENTRY, "+PLATE:1,\"" PLATE "\",93\r\n");
	zassert_equal(state(HW_LANE_ENTRY), LANE_IDENTIFYING);
	zassert_equal(w.enables[HW_LANE_ENTRY], 0);
	zassert_str_equal(w.tx[HW_UART_CAMERA_ENTRY], "AT+CAPTURE=1\r\nAT+CAPTURE=2\r\n");
}

ZTEST(lane, test_late_upper_limit_never_gives_green)
{
	to_opening();
	w.in[HW_LANE_ENTRY][HW_IN_LIMIT_LOWER] = 0;
	w.in[HW_LANE_ENTRY][HW_IN_LIMIT_UPPER] = 1;
	w.now += LANE_MOTOR_BUDGET_MS + 1;
	step(HW_LANE_ENTRY);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
}

ZTEST(lane, test_late_passage_never_creates_session)
{
	to_opening();
	reach_upper(HW_LANE_ENTRY);
	w.in[HW_LANE_ENTRY][HW_IN_BEAM] = 1;
	step(HW_LANE_ENTRY);
	w.now += LANE_PASSAGE_BUDGET_MS + 1;
	w.in[HW_LANE_ENTRY][HW_IN_BEAM] = 0;
	w.dist[HW_LANE_ENTRY] = FAR;
	step(HW_LANE_ENTRY);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
	zassert_equal(session_find(PLATE, NULL), -ENOENT);
}

ZTEST(lane, test_beam_is_rechecked_before_energizing_close)
{
	to_opening();
	reach_upper(HW_LANE_ENTRY);
	w.in[HW_LANE_ENTRY][HW_IN_BEAM] = 1;
	step(HW_LANE_ENTRY);
	w.in[HW_LANE_ENTRY][HW_IN_BEAM] = 0;
	w.dist[HW_LANE_ENTRY] = FAR;
	w.beam_when_green_off = true;
	step(HW_LANE_ENTRY);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
	zassert_equal(w.enables[HW_LANE_ENTRY], 1, "só houve acionamento para abrir");
}

ZTEST(lane, test_display_timeout_prevents_charging)
{
	start_display();
	display_status_fake.return_val = -EAGAIN;
	run(HW_LANE_EXIT, 1100);
	assert_assistance(HW_LANE_EXIT, LANE_ASSIST);
	zassert_equal(payment_start_fake.call_count, 0);
}

ZTEST(lane, test_stale_distance_does_not_authorize_paid)
{
	to_paying();
	w.dist_err = -ENODATA;
	step(HW_LANE_EXIT);
	zassert_false(payment_poll_fake.arg1_val);
	zassert_false(w.out[HW_LANE_EXIT][HW_OUT_MOTOR_ENABLE]);
	w.dist_err = 0;
	payment_poll_fake.return_val = PAYMENT_EV_PAID;
	unsigned int next_poll = payment_poll_fake.call_count;

	step(HW_LANE_EXIT);
	zassert_true(payment_poll_fake.arg1_history[next_poll]);
	zassert_equal(state(HW_LANE_EXIT), LANE_OPENING);
}

ZTEST(lane, test_departure_wins_over_a_buffered_paid)
{
	to_paying();
	w.dist[HW_LANE_EXIT] = FAR;
	payment_poll_fake.return_val = PAYMENT_EV_PAID;
	FFF_RESET_HISTORY();
	step(HW_LANE_EXIT);
	assert_assistance(HW_LANE_EXIT, LANE_ASSIST);
	zassert_false(payment_poll_fake.arg1_val);
	zassert_true(called_at((void *)payment_abort) < called_at((void *)payment_poll));
}

ZTEST(lane, test_display_failure_prevents_charging)
{
	start_display();
	display_status_fake.return_val = -EIO;
	step(HW_LANE_EXIT);
	assert_assistance(HW_LANE_EXIT, LANE_ASSIST);
	zassert_equal(payment_start_fake.call_count, 0);
}

ZTEST(lane, test_departure_while_drawing_prevents_charging)
{
	start_display();
	w.dist[HW_LANE_EXIT] = FAR;
	step(HW_LANE_EXIT);
	assert_assistance(HW_LANE_EXIT, LANE_ASSIST);
	zassert_equal(payment_start_fake.call_count, 0);
}

ZTEST(lane, test_failed_motor_write_does_not_skip_safe_lights)
{
	to_opening();
	reach_upper(HW_LANE_ENTRY);
	w.fail_output = HW_OUT_MOTOR_ENABLE;
	lane_fail(HW_LANE_ENTRY, w.now);
	assert_assistance(HW_LANE_ENTRY, LANE_FAULT);
}
