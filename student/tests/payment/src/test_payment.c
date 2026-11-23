/*
 * test_payment.c — máquina de cobrança (payment.c): REQ-05, REQ-11, REQ-12, REQ-13.
 *
 * COMO FUNCIONA O "CENÁRIO FALSO" DESTE ARQUIVO
 *   payment.c conversa com o mundo por DUAS fronteiras, ambas trocadas por falsos:
 *     1) payment_hw_*  (UART do terminal): fake_send guarda o que o firmware
 *        enviou; fake_read entrega os bytes que o teste "fez chegar" com push_*().
 *     2) a tabela do display (struct payment_display_ops): falsos que registram a
 *        ORDEM das chamadas e os argumentos (placa, valor) e deixam o teste
 *        escolher se o desenho termina (OK), demora (PENDING) ou falha.
 *   O tempo `now` (ms) é passado explicitamente: prazos de 1000 ms ficam exatos.
 *
 * TEMPOS USADOS: a passagem na entrada é em t = 0. O display termina a placa em
 * T_DRAWN = 2*U + 1 (U = unidade da tarifa), então são 3 unidades = 400 centavos.
 * O primeiro CHARGE sai em T_SEND = T_DRAWN + 1 e vence 1000 ms depois.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fff.h>
DEFINE_FFF_GLOBALS;
#include <zephyr/ztest.h>

#include "payment.h"
#include "payment_hw.h"
#include "payment_uuid.h"

FAKE_VALUE_FUNC(int, payment_hw_init);
FAKE_VALUE_FUNC(int, payment_hw_send, const uint8_t *, size_t);
FAKE_VALUE_FUNC(int, payment_hw_read, uint8_t *, size_t);
FAKE_VALUE_FUNC(int, display_draw_plate, const char *);
FAKE_VALUE_FUNC(int, display_draw_charge, const char *, uint32_t);
FAKE_VALUE_FUNC(enum equipment_result, display_poll, int64_t);
FAKE_VOID_FUNC(display_cancel);

#define U ((int64_t)CONFIG_PARKING_TARIFF_UNIT_MS)
#define PLATE "ABC1D23"
#define T_DRAWN (2 * U + 1)
#define T_SEND (T_DRAWN + 1)
#define LOG_MAX 12
#define RX_MAX 512

static const struct payment_display_ops ops = {
	.draw_plate = display_draw_plate,
	.draw_charge = display_draw_charge,
	.poll = display_poll,
	.cancel = display_cancel,
};

/* O que foi enviado ao terminal. */
static char tx_text[LOG_MAX][96];
static unsigned int tx_count;
static int send_result;
static bool cancel_saw_idle; /* quando o CANCEL saiu, o UUID já estava invalidado? */

/* Bytes que o terminal "enviou" e o firmware ainda não leu. */
static uint8_t rx[RX_MAX];
static size_t rx_len;
static size_t rx_pos;
static int read_result;
static bool flood;

/* Ordem das chamadas e argumentos vistos no display. */
static const char *events[LOG_MAX];
static unsigned int event_count;
static int draw_plate_result;
static int draw_charge_result;
static char shown_plate[PARKING_PLATE_SIZE];
static uint32_t shown_amount;

static void log_event(const char *name)
{
	if (event_count < LOG_MAX) {
		events[event_count++] = name;
	}
}

static int fake_send(const uint8_t *data, size_t len)
{
	if (send_result == 0 && tx_count < LOG_MAX && len < sizeof(tx_text[0])) {
		memcpy(tx_text[tx_count], data, len);
		tx_text[tx_count][len] = '\0';
		tx_count++;
		log_event(strncmp((const char *)data, "P1 CANCEL", 9) == 0 ? "cancel" : "send");
		if (strncmp((const char *)data, "P1 CANCEL", 9) == 0) {
			cancel_saw_idle = (payment_poll(0) == EQUIPMENT_FAILED);
		}
	}
	return send_result;
}

static int fake_read(uint8_t *buf, size_t max)
{
	size_t count;

	if (read_result != 0) {
		return read_result;
	}
	if (flood) {
		memset(buf, 'X', max);
		return (int)max;
	}
	count = rx_len - rx_pos;
	if (count > max) {
		count = max;
	}
	memcpy(buf, &rx[rx_pos], count);
	rx_pos += count;
	return (int)count;
}

static int fake_draw_plate(const char *plate)
{
	log_event("draw_plate");
	memcpy(shown_plate, plate, PARKING_PLATE_SIZE);
	return draw_plate_result;
}

static int fake_draw_charge(const char *plate, uint32_t amount)
{
	log_event("draw_charge");
	memcpy(shown_plate, plate, PARKING_PLATE_SIZE);
	shown_amount = amount;
	return draw_charge_result;
}

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(payment_hw_init);
	RESET_FAKE(payment_hw_send);
	RESET_FAKE(payment_hw_read);
	RESET_FAKE(display_draw_plate);
	RESET_FAKE(display_draw_charge);
	RESET_FAKE(display_poll);
	RESET_FAKE(display_cancel);
	FFF_RESET_HISTORY();
	payment_hw_send_fake.custom_fake = fake_send;
	payment_hw_read_fake.custom_fake = fake_read;
	display_draw_plate_fake.custom_fake = fake_draw_plate;
	display_draw_charge_fake.custom_fake = fake_draw_charge;
	display_poll_fake.return_val = EQUIPMENT_OK; /* por padrão, os desenhos terminam na hora */
	memset(tx_text, 0, sizeof(tx_text));
	memset(rx, 0, sizeof(rx));
	memset(events, 0, sizeof(events));
	memset(shown_plate, 0, sizeof(shown_plate));
	tx_count = 0U;
	event_count = 0U;
	rx_len = 0U;
	rx_pos = 0U;
	send_result = 0;
	read_result = 0;
	draw_plate_result = 0;
	draw_charge_result = 0;
	shown_amount = 0U;
	cancel_saw_idle = false;
	flood = false;
	zassert_ok(payment_init(&ops));
}

ZTEST_SUITE(payment, NULL, NULL, before_each, NULL, NULL);

/* ---- auxiliares ---------------------------------------------------------- */

static void push_text(const char *text)
{
	size_t len = strlen(text);

	zassert_true(rx_len + len <= RX_MAX);
	memcpy(&rx[rx_len], text, len);
	rx_len += len;
}

static void push_result(const char *uuid, const char *status, unsigned int amount)
{
	char line[128];

	snprintf(line, sizeof(line), "P1 RESULT %s %s %u BRL\n", uuid, status, amount);
	push_text(line);
}

/* UUID da cobrança enviada no log `index` ("P1 CHARGE <uuid> ..."). */
static const char *uuid_in_tx(unsigned int index)
{
	static char uuid[PAYMENT_UUID_SIZE];

	zassert_true(tx_count > index);
	memcpy(uuid, &tx_text[index][10], PAYMENT_UUID_LEN);
	uuid[PAYMENT_UUID_LEN] = '\0';
	return uuid;
}

/* Leva uma cobrança até o envio do 1º CHARGE; deixa tudo em WAITING. */
static void run_to_waiting(void)
{
	zassert_ok(payment_start(PLATE, 0));
	zassert_equal(payment_poll(T_DRAWN - 2), EQUIPMENT_PENDING); /* pede a placa */
	zassert_equal(payment_poll(T_DRAWN), EQUIPMENT_PENDING);     /* congela e pede a cobrança */
	zassert_equal(payment_poll(T_SEND), EQUIPMENT_PENDING);      /* envia o CHARGE */
	zassert_equal(tx_count, 1U);
}

static void assert_event(unsigned int index, const char *name)
{
	zassert_true(event_count > index);
	zassert_str_equal(events[index], name);
}

/* ---- sequência e valor --------------------------------------------------- */

ZTEST(payment, test_sequence_plate_then_freeze_then_charge_screen_then_send)
{
	char expected[96];

	run_to_waiting();
	zassert_equal(event_count, 3U);
	assert_event(0, "draw_plate");
	assert_event(1, "draw_charge");
	assert_event(2, "send");
	zassert_str_equal(shown_plate, PLATE);
	zassert_equal(shown_amount, 400U); /* 3 unidades: 200 + 2*100 */
	snprintf(expected, sizeof(expected), "P1 CHARGE %s ABC1D23 400 BRL\n", uuid_in_tx(0));
	zassert_str_equal(tx_text[0], expected);
	zassert_true(payment_uuid_valid(uuid_in_tx(0)));
}

ZTEST(payment, test_paid_with_current_uuid_and_exact_amount_opens)
{
	run_to_waiting();
	push_result(uuid_in_tx(0), "PAID", 400);
	zassert_equal(payment_poll(T_SEND + 120), EQUIPMENT_OK);
	zassert_equal(payment_poll(T_SEND + 130), EQUIPMENT_FAILED); /* resultado de uma vez só */
	zassert_equal(tx_count, 1U);                                  /* nenhum CANCEL depois do OK */
}

ZTEST(payment, test_charge_is_not_sent_until_the_display_finishes)
{
	zassert_ok(payment_start(PLATE, 0));
	display_poll_fake.return_val = EQUIPMENT_PENDING;
	zassert_equal(payment_poll(10), EQUIPMENT_PENDING);    /* pede a placa */
	zassert_equal(payment_poll(20), EQUIPMENT_PENDING);    /* placa ainda desenhando */
	zassert_equal(display_draw_charge_fake.call_count, 0U); /* tarifa ainda não congelada */
	display_poll_fake.return_val = EQUIPMENT_OK;
	zassert_equal(payment_poll(30), EQUIPMENT_PENDING);    /* placa pronta: pede a cobrança */
	display_poll_fake.return_val = EQUIPMENT_PENDING;
	zassert_equal(payment_poll(40), EQUIPMENT_PENDING);    /* cobrança ainda desenhando */
	zassert_equal(tx_count, 0U);                            /* nada enviado ao terminal */
	display_poll_fake.return_val = EQUIPMENT_OK;
	zassert_equal(payment_poll(50), EQUIPMENT_PENDING);
	zassert_equal(tx_count, 1U);
}

ZTEST(payment, test_amount_is_frozen_when_the_plate_is_drawn_and_never_changes)
{
	zassert_ok(payment_start(PLATE, 0));
	display_poll_fake.return_val = EQUIPMENT_PENDING;
	zassert_equal(payment_poll(0), EQUIPMENT_PENDING);
	zassert_equal(payment_poll(5 * U), EQUIPMENT_PENDING); /* o display demorou 5 unidades */
	display_poll_fake.return_val = EQUIPMENT_OK;
	zassert_equal(payment_poll(5 * U + 10), EQUIPMENT_PENDING); /* congela AQUI: 6 unidades */
	zassert_equal(shown_amount, 700U);
	zassert_equal(payment_poll(5 * U + 20), EQUIPMENT_PENDING); /* envia */
	/* muito tempo depois, a retransmissão leva o MESMO valor e o MESMO UUID */
	zassert_equal(payment_poll(5 * U + 20 + 1000), EQUIPMENT_PENDING);
	zassert_equal(tx_count, 2U);
	zassert_str_equal(tx_text[0], tx_text[1]);
	zassert_equal(shown_amount, 700U);
	zassert_equal(display_draw_charge_fake.call_count, 1U);
}

ZTEST(payment, test_more_than_9999_units_asks_for_assistance_and_charges_nothing)
{
	zassert_ok(payment_start(PLATE, 0));
	zassert_equal(payment_poll(0), EQUIPMENT_PENDING);
	zassert_equal(payment_poll(9999 * U + 1), EQUIPMENT_FAILED);
	zassert_equal(display_draw_charge_fake.call_count, 0U);
	zassert_equal(tx_count, 0U);

	/* exatamente 9999 unidades ainda cobra o máximo do terminal: 1.000.000 centavos */
	zassert_ok(payment_start(PLATE, 0));
	zassert_equal(payment_poll(0), EQUIPMENT_PENDING);
	zassert_equal(payment_poll(9999 * U), EQUIPMENT_PENDING);
	zassert_equal(shown_amount, 1000000U);
}

/* ---- respostas que NUNCA abrem a cancela -------------------------------- */

ZTEST(payment, test_stale_wrong_amount_cancelled_malformed_and_error_never_open)
{
	char other[PAYMENT_UUID_SIZE];

	payment_uuid_make(0xFFFFFFULL, other);
	run_to_waiting();
	push_result(other, "PAID", 400);                     /* UUID antigo (payment-stale) */
	push_result(uuid_in_tx(0), "PAID", 401);             /* valor diferente */
	push_result(uuid_in_tx(0), "PAID", 399);
	push_result(other, "CANCELLED", 400);                /* cancelamento de outro UUID */
	push_text("P1 RESULT ");                             /* resposta quebrada (MALFORMED) */
	push_text(uuid_in_tx(0));
	push_text(" PAID broken BRL\n");
	push_text("P1 ERROR UNKNOWN\n");                     /* diagnóstico: nunca decide */
	push_text("GARBAGE\n");
	/* são ~350 bytes de lixo; cada poll lê no máximo 128 (limite deliberado): esvazia em vários */
	for (int i = 0; i < 4; ++i) {
		zassert_equal(payment_poll(T_SEND + 10 + i), EQUIPMENT_PENDING);
	}
	zassert_equal(rx_pos, rx_len);
	zassert_equal(tx_count, 1U);
	/* e a resposta certa, depois de todo esse lixo, ainda vale */
	push_result(uuid_in_tx(0), "PAID", 400);
	zassert_equal(payment_poll(T_SEND + 20), EQUIPMENT_OK);
}

ZTEST(payment, test_cancelled_for_current_charge_fails_without_sending_cancel)
{
	run_to_waiting();
	push_result(uuid_in_tx(0), "CANCELLED", 400);
	zassert_equal(payment_poll(T_SEND + 10), EQUIPMENT_FAILED);
	zassert_equal(tx_count, 1U); /* o terminal já cancelou: nada a desfazer */
}

ZTEST(payment, test_first_decisive_frame_in_a_read_wins)
{
	run_to_waiting();
	push_result(uuid_in_tx(0), "CANCELLED", 400);
	push_result(uuid_in_tx(0), "PAID", 400);
	zassert_equal(payment_poll(T_SEND + 10), EQUIPMENT_FAILED);

	tx_count = 0U;
	zassert_ok(payment_start(PLATE, 0));
	zassert_equal(payment_poll(T_DRAWN - 2), EQUIPMENT_PENDING);
	zassert_equal(payment_poll(T_DRAWN), EQUIPMENT_PENDING);
	zassert_equal(payment_poll(T_SEND), EQUIPMENT_PENDING);
	push_result(uuid_in_tx(0), "PAID", 400);
	push_result(uuid_in_tx(0), "PAID", 400);
	zassert_equal(payment_poll(T_SEND + 10), EQUIPMENT_OK);
}

ZTEST(payment, test_duplicate_paid_after_success_is_ignored_by_the_next_charge)
{
	char first[PAYMENT_UUID_SIZE];

	run_to_waiting();
	push_result(uuid_in_tx(0), "PAID", 400);
	zassert_equal(payment_poll(T_SEND + 10), EQUIPMENT_OK);
	strcpy(first, uuid_in_tx(0));

	push_result(first, "PAID", 400); /* o terminal repete o PAID (payment-duplicate) */
	tx_count = 0U;
	run_to_waiting();                /* nova saída: a UART é limpa ao começar */
	zassert_true(strcmp(first, uuid_in_tx(0)) != 0);
	zassert_equal(payment_poll(T_SEND + 10), EQUIPMENT_PENDING);
}

/* ---- retransmissão e esgotamento ---------------------------------------- */

ZTEST(payment, test_resends_same_charge_three_times_then_cancels_and_fails)
{
	char charge[96];
	char expected_cancel[96];

	run_to_waiting();
	strcpy(charge, tx_text[0]);
	zassert_equal(payment_poll(T_SEND + 999), EQUIPMENT_PENDING);
	zassert_equal(tx_count, 1U);
	zassert_equal(payment_poll(T_SEND + 1000), EQUIPMENT_PENDING); /* envio 2 */
	zassert_str_equal(tx_text[1], charge);
	zassert_equal(payment_poll(T_SEND + 1999), EQUIPMENT_PENDING);
	zassert_equal(payment_poll(T_SEND + 2000), EQUIPMENT_PENDING); /* envio 3 */
	zassert_str_equal(tx_text[2], charge);
	zassert_equal(payment_poll(T_SEND + 2999), EQUIPMENT_PENDING);
	zassert_equal(payment_poll(T_SEND + 3000), EQUIPMENT_FAILED);  /* esgotou */

	snprintf(expected_cancel, sizeof(expected_cancel), "P1 CANCEL %s\n", uuid_in_tx(0));
	zassert_equal(tx_count, 4U);
	zassert_str_equal(tx_text[3], expected_cancel);
	assert_event(5, "cancel");
	zassert_true(cancel_saw_idle); /* o UUID foi invalidado ANTES de enviar o CANCEL */
	/* uma resposta tardia do UUID desistido não faz mais nada */
	push_result(uuid_in_tx(0), "PAID", 400);
	zassert_equal(payment_poll(T_SEND + 3010), EQUIPMENT_FAILED);
}

ZTEST(payment, test_delayed_paid_between_retransmissions_is_accepted_in_fragments)
{
	char line[128];
	int64_t now = T_SEND;

	run_to_waiting();
	snprintf(line, sizeof(line), "P1 RESULT %s PAID 400 BRL\n", uuid_in_tx(0));
	zassert_equal(payment_poll(T_SEND + 1000), EQUIPMENT_PENDING); /* retransmitiu */
	now = T_SEND + 1500; /* payment-delayed: a resposta demora 1500 ms e chega em pedaços de 2 bytes */
	for (size_t i = 0U; line[i] != '\0'; i += 2U) {
		char piece[3] = { line[i], line[i + 1U], '\0' };

		zassert_equal(payment_poll(now), EQUIPMENT_PENDING);
		push_text(piece);
		now += 10;
	}
	zassert_equal(payment_poll(now), EQUIPMENT_OK);
	zassert_equal(tx_count, 2U);
}

ZTEST(payment, test_paid_read_exactly_at_the_final_deadline_is_still_accepted)
{
	run_to_waiting();
	zassert_equal(payment_poll(T_SEND + 1000), EQUIPMENT_PENDING);
	zassert_equal(payment_poll(T_SEND + 2000), EQUIPMENT_PENDING);
	push_result(uuid_in_tx(0), "PAID", 400);
	/* mesmo poll em que o prazo final venceria: aceitar evita "cliente pagou e a cancela não abriu" */
	zassert_equal(payment_poll(T_SEND + 3000), EQUIPMENT_OK);
	zassert_equal(tx_count, 3U); /* nenhum CANCEL */
}

/* ---- cancelamento -------------------------------------------------------- */

ZTEST(payment, test_cancel_while_waiting_invalidates_and_sends_cancel)
{
	char expected[96];
	char uuid[PAYMENT_UUID_SIZE];

	run_to_waiting();
	strcpy(uuid, uuid_in_tx(0));
	payment_cancel();
	snprintf(expected, sizeof(expected), "P1 CANCEL %s\n", uuid);
	zassert_equal(tx_count, 2U);
	zassert_str_equal(tx_text[1], expected);
	zassert_true(cancel_saw_idle);
	push_result(uuid, "PAID", 400); /* PAID depois do cancelamento nunca abre */
	zassert_equal(payment_poll(T_SEND + 10), EQUIPMENT_FAILED);
	payment_cancel(); /* segundo cancel: nada a fazer */
	zassert_equal(tx_count, 2U);
}

ZTEST(payment, test_cancel_while_drawing_cancels_the_display_only)
{
	zassert_ok(payment_start(PLATE, 0));
	display_poll_fake.return_val = EQUIPMENT_PENDING;
	zassert_equal(payment_poll(10), EQUIPMENT_PENDING); /* PLATE */
	payment_cancel();
	zassert_equal(display_cancel_fake.call_count, 1U);

	zassert_ok(payment_start(PLATE, 0));
	display_poll_fake.return_val = EQUIPMENT_OK;
	zassert_equal(payment_poll(20), EQUIPMENT_PENDING);
	zassert_equal(payment_poll(30), EQUIPMENT_PENDING); /* agora em SCREEN */
	display_poll_fake.return_val = EQUIPMENT_PENDING;
	payment_cancel();
	zassert_equal(display_cancel_fake.call_count, 2U);
	zassert_equal(tx_count, 0U); /* nada foi cobrado, então não há o que cancelar no terminal */
}

ZTEST(payment, test_cancel_before_first_poll_and_when_idle_do_nothing)
{
	payment_cancel(); /* ocioso */
	zassert_ok(payment_start(PLATE, 0));
	payment_cancel(); /* STARTING */
	zassert_equal(display_cancel_fake.call_count, 0U);
	zassert_equal(display_draw_plate_fake.call_count, 0U);
	zassert_equal(tx_count, 0U);
	zassert_equal(payment_poll(10), EQUIPMENT_FAILED);
	zassert_ok(payment_start(PLATE, 0)); /* pode cobrar de novo */
}

/* ---- erros de hardware (só os falsos conseguem provocá-los) ------------- */

ZTEST(payment, test_display_failures_fail_the_payment)
{
	draw_plate_result = -EIO;
	zassert_ok(payment_start(PLATE, 0));
	zassert_equal(payment_poll(10), EQUIPMENT_FAILED);

	draw_plate_result = 0;
	draw_charge_result = -EIO;
	zassert_ok(payment_start(PLATE, 0));
	zassert_equal(payment_poll(10), EQUIPMENT_PENDING);
	zassert_equal(payment_poll(20), EQUIPMENT_FAILED);

	draw_charge_result = 0;
	display_poll_fake.return_val = EQUIPMENT_FAILED; /* falha no meio do desenho da placa */
	zassert_ok(payment_start(PLATE, 0));
	zassert_equal(payment_poll(30), EQUIPMENT_PENDING);
	zassert_equal(payment_poll(40), EQUIPMENT_FAILED);

	display_poll_fake.return_val = EQUIPMENT_OK; /* ...e no desenho da cobrança */
	zassert_ok(payment_start(PLATE, 0));
	zassert_equal(payment_poll(50), EQUIPMENT_PENDING);
	zassert_equal(payment_poll(60), EQUIPMENT_PENDING);
	display_poll_fake.return_val = EQUIPMENT_FAILED;
	zassert_equal(payment_poll(70), EQUIPMENT_FAILED);
	zassert_equal(tx_count, 0U);
}

ZTEST(payment, test_send_error_on_first_charge_fails_without_cancel)
{
	zassert_ok(payment_start(PLATE, 0));
	zassert_equal(payment_poll(0), EQUIPMENT_PENDING);
	zassert_equal(payment_poll(1), EQUIPMENT_PENDING);
	send_result = -EIO;
	zassert_equal(payment_poll(2), EQUIPMENT_FAILED);
	zassert_equal(payment_hw_send_fake.call_count, 1U); /* só a tentativa de CHARGE */
}

ZTEST(payment, test_send_error_on_retransmission_fails_and_still_tries_to_cancel)
{
	run_to_waiting();
	send_result = -EIO;
	zassert_equal(payment_poll(T_SEND + 1000), EQUIPMENT_FAILED);
	zassert_equal(payment_hw_send_fake.call_count, 3U); /* CHARGE, reenvio (falhou), CANCEL */
	send_result = 0;
	zassert_equal(payment_poll(T_SEND + 1010), EQUIPMENT_FAILED); /* já invalidado */
}

ZTEST(payment, test_read_errors_fail_while_waiting_and_while_starting)
{
	run_to_waiting();
	read_result = -EIO;
	zassert_equal(payment_poll(T_SEND + 10), EQUIPMENT_FAILED);
	zassert_equal(payment_hw_send_fake.call_count, 2U); /* tentou o CANCEL */

	zassert_ok(payment_start(PLATE, 0)); /* ao começar, a UART é esvaziada: erro => falha */
	zassert_equal(payment_poll(10), EQUIPMENT_FAILED);
	zassert_equal(display_draw_plate_fake.call_count, 1U); /* só o da 1ª cobrança */
}

ZTEST(payment, test_endless_stream_does_not_block_the_poll)
{
	run_to_waiting();
	flood = true;
	payment_hw_read_fake.call_count = 0U;
	zassert_equal(payment_poll(T_SEND + 10), EQUIPMENT_PENDING);
	zassert_equal(payment_hw_read_fake.call_count, (unsigned int)PAYMENT_READ_ROUNDS);
}

/* ---- API: validações, UUIDs e reset ------------------------------------- */

ZTEST(payment, test_start_validation)
{
	static const char lowercase[PARKING_PLATE_SIZE] = "abc1d23";
	static const char too_short[PARKING_PLATE_SIZE] = "ABC1D2";

	zassert_equal(payment_start(lowercase, 0), -EINVAL);
	zassert_equal(payment_start(too_short, 0), -EINVAL);
	zassert_equal(payment_start(NULL, 0), -EINVAL);
	zassert_ok(payment_start(PLATE, 0));
	zassert_equal(payment_start(PLATE, 0), -EBUSY); /* nunca duas cobranças ao mesmo tempo */
	zassert_equal(payment_poll(10), EQUIPMENT_PENDING);
	zassert_equal(payment_start(PLATE, 0), -EBUSY);
}

ZTEST(payment, test_poll_without_charge_never_authorizes)
{
	zassert_equal(payment_poll(0), EQUIPMENT_FAILED);
	zassert_equal(payment_hw_read_fake.call_count, 0U);
}

ZTEST(payment, test_each_attempt_gets_a_new_valid_uuid)
{
	char first[PAYMENT_UUID_SIZE];

	run_to_waiting();
	strcpy(first, uuid_in_tx(0));
	payment_cancel();
	tx_count = 0U;
	run_to_waiting();
	zassert_true(payment_uuid_valid(first));
	zassert_true(strcmp(first, uuid_in_tx(0)) != 0);
}

ZTEST(payment, test_init_rejects_incomplete_tables_and_uart_errors)
{
	struct payment_display_ops broken = ops;

	zassert_equal(payment_init(NULL), -EINVAL);
	zassert_equal(payment_start(PLATE, 0), -ENODEV);

	broken.draw_plate = NULL;
	zassert_equal(payment_init(&broken), -EINVAL);
	broken = ops;
	broken.draw_charge = NULL;
	zassert_equal(payment_init(&broken), -EINVAL);
	broken = ops;
	broken.poll = NULL;
	zassert_equal(payment_init(&broken), -EINVAL);
	broken = ops;
	broken.cancel = NULL;
	zassert_equal(payment_init(&broken), -EINVAL);

	payment_hw_init_fake.return_val = -ENODEV;
	zassert_equal(payment_init(&ops), -ENODEV);
	zassert_equal(payment_start(PLATE, 0), -ENODEV);

	payment_hw_init_fake.return_val = 0;
	zassert_ok(payment_init(&ops));
	zassert_ok(payment_start(PLATE, 0));
}

ZTEST(payment, test_reset_forgets_everything_but_never_reuses_uuids)
{
	char before[PAYMENT_UUID_SIZE];

	run_to_waiting();
	strcpy(before, uuid_in_tx(0));
	payment_reset();
	zassert_equal(payment_poll(T_SEND + 10), EQUIPMENT_FAILED);
	push_result(before, "PAID", 400); /* resposta atrasada de antes do reset */
	tx_count = 0U;
	run_to_waiting();
	zassert_true(strcmp(before, uuid_in_tx(0)) != 0);
	zassert_equal(payment_poll(T_SEND + 10), EQUIPMENT_PENDING); /* o PAID antigo não vale */
}
