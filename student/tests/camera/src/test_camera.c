/*
 * test_camera.c — testes da máquina de captura (camera.c): REQ-02, REQ-11,
 * partes de REQ-03 e REQ-13.
 *
 * COMO FUNCIONA O "CENÁRIO FALSO" DESTE ARQUIVO
 *   camera.c fala com o mundo só por três funções (camera_hw_*). Aqui elas são
 *   FALSOS do FFF:
 *     - camera_hw_send : guarda o que o firmware "mandou" (para conferirmos
 *                        os comandos AT+CAPTURE=<id>) e pode simular falha;
 *     - camera_hw_read : entrega os bytes que o teste "fez chegar" com push_*(),
 *                        por faixa, ou simula um erro de UART.
 *   O tempo (`now`, em ms) é passado explicitamente em cada chamada: o teste
 *   controla o relógio, então prazos de 1000 ms são exatos e sem espera real.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fff.h>
DEFINE_FFF_GLOBALS;
DEFINE_FFF_GLOBALS;
#include <zephyr/ztest.h>

#include "camera.h"
#include "camera_hw.h"

FAKE_VALUE_FUNC(int, camera_hw_init);
FAKE_VALUE_FUNC(int, camera_hw_send, unsigned int, const uint8_t *, size_t);
FAKE_VALUE_FUNC(int, camera_hw_read, unsigned int, uint8_t *, size_t);

#define LOG_MAX 12
#define RX_MAX 512

/* O que o firmware enviou (um texto por chamada de send) e para qual faixa. */
static char tx_text[LOG_MAX][32];
static unsigned int tx_lane[LOG_MAX];
static unsigned int tx_count;
static int send_result;

/* Bytes que a "câmera" já entregou à UART e o firmware ainda não leu, por faixa. */
static uint8_t rx[PARKING_LANES][RX_MAX];
static size_t rx_len[PARKING_LANES];
static size_t rx_pos[PARKING_LANES];
static int read_result; /* diferente de 0: a leitura falha com este erro */
static bool flood;      /* true: a UART tem sempre bytes novos (fluxo sem fim) */

static int fake_send(unsigned int lane, const uint8_t *data, size_t len)
{
	if (send_result == 0 && tx_count < LOG_MAX && len < sizeof(tx_text[0])) {
		memcpy(tx_text[tx_count], data, len);
		tx_text[tx_count][len] = '\0';
		tx_lane[tx_count] = lane;
		tx_count++;
	}
	return send_result;
}

static int fake_read(unsigned int lane, uint8_t *buf, size_t max)
{
	size_t count;

	if (read_result != 0) {
		return read_result;
	}
	if (flood) {
		memset(buf, 'X', max);
		return (int)max;
	}
	count = rx_len[lane] - rx_pos[lane];
	if (count > max) {
		count = max;
	}
	memcpy(buf, &rx[lane][rx_pos[lane]], count);
	rx_pos[lane] += count;
	return (int)count;
}

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(camera_hw_init);
	RESET_FAKE(camera_hw_send);
	RESET_FAKE(camera_hw_read);
	FFF_RESET_HISTORY();
	camera_hw_send_fake.custom_fake = fake_send;
	camera_hw_read_fake.custom_fake = fake_read;
	memset(tx_text, 0, sizeof(tx_text));
	memset(tx_lane, 0, sizeof(tx_lane));
	memset(rx, 0, sizeof(rx));
	memset(rx_len, 0, sizeof(rx_len));
	memset(rx_pos, 0, sizeof(rx_pos));
	tx_count = 0U;
	send_result = 0;
	read_result = 0;
	flood = false;
	camera_reset();
}

/* Faz chegar `text` na UART da faixa (como se a câmera tivesse enviado). */
static void push_text(enum parking_lane_id lane, const char *text)
{
	size_t len = strlen(text);

	zassert_true(rx_len[lane] + len <= RX_MAX);
	memcpy(&rx[lane][rx_len[lane]], text, len);
	rx_len[lane] += len;
}

static void push_plate(enum parking_lane_id lane, unsigned int id, const char *plate,
		       unsigned int confidence)
{
	char line[64];

	snprintf(line, sizeof(line), "+PLATE:%u,\"%s\",%u\r\n", id, plate, confidence);
	push_text(lane, line);
}

static void assert_sent(unsigned int index, enum parking_lane_id lane, const char *text)
{
	zassert_true(tx_count > index);
	zassert_equal(tx_lane[index], (unsigned int)lane);
	zassert_str_equal(tx_text[index], text);
}

static enum equipment_result poll(enum parking_lane_id lane, int64_t now)
{
	char plate[PARKING_PLATE_SIZE];

	return camera_capture_poll(lane, now, plate);
}

ZTEST_SUITE(camera_capture, NULL, NULL, before_each, NULL, NULL);

/* ---- caminho normal ----------------------------------------------------- */

ZTEST(camera_capture, test_start_sends_capture_command_with_fresh_id)
{
	zassert_ok(camera_capture_start(PARKING_EXIT, 0));
	assert_sent(0, PARKING_EXIT, "AT+CAPTURE=1\r\n");
	zassert_equal(tx_count, 1U);
	zassert_equal(poll(PARKING_EXIT, 10), EQUIPMENT_PENDING);
}

ZTEST(camera_capture, test_accepts_valid_plate_for_current_id)
{
	char plate[PARKING_PLATE_SIZE] = { 0 };

	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	push_text(PARKING_ENTRY, "OK\r\n"); /* OK só confirma o pedido: não é placa */
	zassert_equal(camera_capture_poll(PARKING_ENTRY, 100, plate), EQUIPMENT_PENDING);
	push_plate(PARKING_ENTRY, 1, "ABC1D23", 93);
	zassert_equal(camera_capture_poll(PARKING_ENTRY, 200, plate), EQUIPMENT_OK);
	zassert_mem_equal(plate, "ABC1D23", PARKING_PLATE_SIZE);
	/* o resultado é de uma vez só: não existe captura em andamento agora */
	zassert_equal(camera_capture_poll(PARKING_ENTRY, 210, plate), EQUIPMENT_FAILED);
}

ZTEST(camera_capture, test_confidence_85_is_accepted_and_84_is_not)
{
	char plate[PARKING_PLATE_SIZE] = { 0 };

	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	push_plate(PARKING_ENTRY, 1, "ABC1D23", 85);
	zassert_equal(camera_capture_poll(PARKING_ENTRY, 10, plate), EQUIPMENT_OK);

	zassert_ok(camera_capture_start(PARKING_ENTRY, 20));
	push_plate(PARKING_ENTRY, 2, "ABC1D23", 84);
	zassert_equal(camera_capture_poll(PARKING_ENTRY, 30, plate), EQUIPMENT_PENDING);
	assert_sent(2, PARKING_ENTRY, "AT+CAPTURE=3\r\n"); /* gastou a tentativa: id novo */
}

ZTEST(camera_capture, test_reply_arriving_in_fragments_is_assembled)
{
	static const char reply[] = "+PLATE:1,\"ABC1D23\",93\r\n";
	char plate[PARKING_PLATE_SIZE] = { 0 };
	int64_t now = 0;

	zassert_ok(camera_capture_start(PARKING_ENTRY, now));
	for (size_t i = 0U; i + 1U < sizeof(reply); i += 2U) {
		char piece[3] = { reply[i], reply[i + 1U], '\0' };

		zassert_equal(camera_capture_poll(PARKING_ENTRY, now, plate), EQUIPMENT_PENDING);
		push_text(PARKING_ENTRY, piece);
		now += 10; /* um pedaço de 2 bytes a cada 10 ms, como o modelo */
	}
	zassert_equal(camera_capture_poll(PARKING_ENTRY, now, plate), EQUIPMENT_OK);
	zassert_mem_equal(plate, "ABC1D23", PARKING_PLATE_SIZE);
}

/* ---- tentativas, confiança e prazo ------------------------------------- */

ZTEST(camera_capture, test_low_confidence_retries_with_new_ids_then_accepts)
{
	char plate[PARKING_PLATE_SIZE] = { 0 };

	/* cenário confidence-retry: 62 -> 79 -> 93 */
	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	push_plate(PARKING_ENTRY, 1, "ABC1D23", 62);
	zassert_equal(camera_capture_poll(PARKING_ENTRY, 10, plate), EQUIPMENT_PENDING);
	push_plate(PARKING_ENTRY, 2, "ABC1D23", 79);
	zassert_equal(camera_capture_poll(PARKING_ENTRY, 20, plate), EQUIPMENT_PENDING);
	push_plate(PARKING_ENTRY, 3, "ABC1D23", 93);
	zassert_equal(camera_capture_poll(PARKING_ENTRY, 30, plate), EQUIPMENT_OK);
	zassert_equal(tx_count, 3U);
	assert_sent(1, PARKING_ENTRY, "AT+CAPTURE=2\r\n");
	assert_sent(2, PARKING_ENTRY, "AT+CAPTURE=3\r\n");
}

ZTEST(camera_capture, test_three_low_confidence_attempts_exhaust_and_fail)
{
	/* cenário low-confidence: 62 -> 79 -> 84, nenhum >= 85 */
	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	push_plate(PARKING_ENTRY, 1, "ABC1D23", 62);
	zassert_equal(poll(PARKING_ENTRY, 10), EQUIPMENT_PENDING);
	push_plate(PARKING_ENTRY, 2, "ABC1D23", 79);
	zassert_equal(poll(PARKING_ENTRY, 20), EQUIPMENT_PENDING);
	push_plate(PARKING_ENTRY, 3, "ABC1D23", 84);
	zassert_equal(poll(PARKING_ENTRY, 30), EQUIPMENT_FAILED);
	zassert_equal(tx_count, 3U); /* nunca passa de 3 tentativas */
	zassert_equal(poll(PARKING_ENTRY, 40), EQUIPMENT_FAILED);
	zassert_equal(tx_count, 3U);
}

ZTEST(camera_capture, test_timeout_consumes_attempts_then_fails)
{
	/* cenário camera-timeout: a câmera só responde OK e nunca manda a placa */
	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	push_text(PARKING_ENTRY, "OK\r\n");
	zassert_equal(poll(PARKING_ENTRY, 999), EQUIPMENT_PENDING);
	zassert_equal(poll(PARKING_ENTRY, 1000), EQUIPMENT_PENDING); /* venceu: tentativa 2 */
	assert_sent(1, PARKING_ENTRY, "AT+CAPTURE=2\r\n");
	zassert_equal(poll(PARKING_ENTRY, 1999), EQUIPMENT_PENDING);
	zassert_equal(poll(PARKING_ENTRY, 2000), EQUIPMENT_PENDING); /* tentativa 3 */
	assert_sent(2, PARKING_ENTRY, "AT+CAPTURE=3\r\n");
	zassert_equal(poll(PARKING_ENTRY, 2999), EQUIPMENT_PENDING);
	zassert_equal(poll(PARKING_ENTRY, 3000), EQUIPMENT_FAILED);
	zassert_equal(tx_count, 3U);
}

ZTEST(camera_capture, test_ok_and_error_do_not_restart_the_deadline)
{
	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	push_text(PARKING_ENTRY, "OK\r\n");
	zassert_equal(poll(PARKING_ENTRY, 500), EQUIPMENT_PENDING);
	push_text(PARKING_ENTRY, "ERROR\r\n");
	zassert_equal(poll(PARKING_ENTRY, 900), EQUIPMENT_PENDING);
	zassert_equal(tx_count, 1U);
	zassert_equal(poll(PARKING_ENTRY, 1000), EQUIPMENT_PENDING); /* mesmo prazo de antes */
	zassert_equal(tx_count, 2U);
}

ZTEST(camera_capture, test_reply_completed_at_or_after_deadline_is_ignored)
{
	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	push_plate(PARKING_ENTRY, 1, "ABC1D23", 93); /* chegou, mas só é lida em t = 1000 */
	zassert_equal(poll(PARKING_ENTRY, 1000), EQUIPMENT_PENDING);
	assert_sent(1, PARKING_ENTRY, "AT+CAPTURE=2\r\n"); /* tentativa 1 venceu; tentativa 2 */
}

/* ---- respostas que NUNCA podem autorizar ------------------------------- */

ZTEST(camera_capture, test_reply_for_other_or_old_id_is_ignored)
{
	char plate[PARKING_PLATE_SIZE] = { 0 };

	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	push_plate(PARKING_ENTRY, 7, "ABC1D23", 99); /* id que ninguém pediu */
	zassert_equal(camera_capture_poll(PARKING_ENTRY, 10, plate), EQUIPMENT_PENDING);

	zassert_equal(poll(PARKING_ENTRY, 1000), EQUIPMENT_PENDING); /* tentativa 2 (id 2) */
	push_plate(PARKING_ENTRY, 1, "ABC1D23", 99); /* resposta TARDIA da tentativa 1 */
	zassert_equal(camera_capture_poll(PARKING_ENTRY, 1010, plate), EQUIPMENT_PENDING);
	push_plate(PARKING_ENTRY, 2, "XYZ9Z99", 90); /* a do id atual vale */
	zassert_equal(camera_capture_poll(PARKING_ENTRY, 1020, plate), EQUIPMENT_OK);
	zassert_mem_equal(plate, "XYZ9Z99", PARKING_PLATE_SIZE);
}

ZTEST(camera_capture, test_malformed_replies_are_ignored)
{
	char plate[PARKING_PLATE_SIZE] = { 0 };

	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	push_text(PARKING_ENTRY, "+PLATE:1,\"abc1d23\",93\r\n");  /* placa inválida */
	push_text(PARKING_ENTRY, "+PLATE:1,\"ABC1D23\",101\r\n"); /* confiança impossível */
	push_text(PARKING_ENTRY, "+PLATE:1,\"ABC1D23\",93");      /* sem terminador */
	zassert_equal(camera_capture_poll(PARKING_ENTRY, 10, plate), EQUIPMENT_PENDING);
	zassert_equal(tx_count, 1U);
}

ZTEST(camera_capture, test_duplicate_reply_in_one_read_spends_only_one_attempt)
{
	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	push_plate(PARKING_ENTRY, 1, "ABC1D23", 50);
	push_plate(PARKING_ENTRY, 1, "ABC1D23", 50);
	zassert_equal(poll(PARKING_ENTRY, 10), EQUIPMENT_PENDING);
	zassert_equal(tx_count, 2U); /* só uma nova tentativa */
}

ZTEST(camera_capture, test_cancel_invalidates_capture_and_late_reply_cannot_authorize)
{
	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	camera_capture_cancel(PARKING_ENTRY);
	zassert_equal(poll(PARKING_ENTRY, 10), EQUIPMENT_FAILED);

	/* a resposta do id 1 chega depois do cancelamento... */
	push_plate(PARKING_ENTRY, 1, "ABC1D23", 99);
	/* ...e uma captura nova a descarta ao começar, usando o id 2 */
	zassert_ok(camera_capture_start(PARKING_ENTRY, 20));
	assert_sent(1, PARKING_ENTRY, "AT+CAPTURE=2\r\n");
	zassert_equal(poll(PARKING_ENTRY, 30), EQUIPMENT_PENDING);
}

/* ---- duas faixas, ids e reset ------------------------------------------ */

ZTEST(camera_capture, test_lanes_are_independent)
{
	char plate[PARKING_PLATE_SIZE] = { 0 };

	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	zassert_ok(camera_capture_start(PARKING_EXIT, 0));
	assert_sent(0, PARKING_ENTRY, "AT+CAPTURE=1\r\n");
	assert_sent(1, PARKING_EXIT, "AT+CAPTURE=1\r\n"); /* cada câmera tem o seu cache de ids */

	push_plate(PARKING_EXIT, 1, "ABC1D23", 93);
	zassert_equal(camera_capture_poll(PARKING_EXIT, 10, plate), EQUIPMENT_OK);
	/* a resposta da saída não toca a entrada; a entrada segue esperando e vence sozinha */
	zassert_equal(poll(PARKING_ENTRY, 10), EQUIPMENT_PENDING);
	zassert_equal(poll(PARKING_ENTRY, 1000), EQUIPMENT_PENDING);
	assert_sent(2, PARKING_ENTRY, "AT+CAPTURE=2\r\n");
}

ZTEST(camera_capture, test_ids_never_repeat_across_captures)
{
	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	camera_capture_cancel(PARKING_ENTRY);
	zassert_ok(camera_capture_start(PARKING_ENTRY, 10));
	assert_sent(0, PARKING_ENTRY, "AT+CAPTURE=1\r\n");
	assert_sent(1, PARKING_ENTRY, "AT+CAPTURE=2\r\n");
}

ZTEST(camera_capture, test_id_limit_999999999)
{
	camera_set_next_id(PARKING_ENTRY, CAMERA_ID_MAX);
	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	assert_sent(0, PARKING_ENTRY, "AT+CAPTURE=999999999\r\n");
	/* acabaram os ids: a nova tentativa não pode inventar um id fora do contrato */
	zassert_equal(poll(PARKING_ENTRY, 1000), EQUIPMENT_FAILED);
	zassert_equal(camera_capture_start(PARKING_ENTRY, 2000), -ENOSPC);
	zassert_equal(poll(PARKING_ENTRY, 2010), EQUIPMENT_FAILED);

	camera_set_next_id((enum parking_lane_id)PARKING_LANES, 5U); /* faixa inválida: ignorada */
}

ZTEST(camera_capture, test_reset_forgets_capture_ids_and_partial_bytes)
{
	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	push_text(PARKING_ENTRY, "+PLATE:1,\"ABC"); /* linha parcial dentro do parser */
	zassert_equal(poll(PARKING_ENTRY, 10), EQUIPMENT_PENDING);
	camera_reset();
	zassert_equal(poll(PARKING_ENTRY, 20), EQUIPMENT_FAILED); /* nada em andamento */
	zassert_ok(camera_capture_start(PARKING_ENTRY, 30));
	assert_sent(1, PARKING_ENTRY, "AT+CAPTURE=1\r\n"); /* ids recomeçam */
}

/* ---- erros de hardware (só os falsos conseguem provocá-los) ------------ */

ZTEST(camera_capture, test_send_error_at_start_is_reported)
{
	send_result = -EIO;
	zassert_equal(camera_capture_start(PARKING_ENTRY, 0), -EIO);
	zassert_equal(poll(PARKING_ENTRY, 10), EQUIPMENT_FAILED);
}

ZTEST(camera_capture, test_send_error_on_retry_fails_the_capture)
{
	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	send_result = -EIO;
	zassert_equal(poll(PARKING_ENTRY, 1000), EQUIPMENT_FAILED);
}

ZTEST(camera_capture, test_read_error_fails_poll_and_start)
{
	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	read_result = -EIO;
	zassert_equal(poll(PARKING_ENTRY, 10), EQUIPMENT_FAILED);
	zassert_equal(camera_capture_start(PARKING_ENTRY, 20), -EIO);
	zassert_equal(tx_count, 1U); /* nenhum pedido novo foi enviado */
}

ZTEST(camera_capture, test_invalid_lane_is_rejected)
{
	char plate[PARKING_PLATE_SIZE] = { 0 };
	enum parking_lane_id bad = (enum parking_lane_id)PARKING_LANES;

	zassert_equal(camera_capture_start(bad, 0), -EINVAL);
	zassert_equal(camera_capture_poll(bad, 0, plate), EQUIPMENT_FAILED);
	camera_capture_cancel(bad); /* não pode quebrar nada */
	zassert_equal(tx_count, 0U);
}

ZTEST(camera_capture, test_endless_stream_does_not_block_the_poll)
{
	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	flood = true;
	camera_hw_read_fake.call_count = 0U;
	zassert_equal(poll(PARKING_ENTRY, 10), EQUIPMENT_PENDING);
	zassert_equal(camera_hw_read_fake.call_count, (unsigned int)CAMERA_READ_ROUNDS);
}

ZTEST(camera_capture, test_init_resets_and_reports_uart_errors)
{
	zassert_ok(camera_capture_start(PARKING_ENTRY, 0));
	camera_hw_init_fake.return_val = 0;
	zassert_ok(camera_init());
	zassert_equal(camera_hw_init_fake.call_count, 1U);
	zassert_equal(poll(PARKING_ENTRY, 10), EQUIPMENT_FAILED); /* init também faz reset */

	camera_hw_init_fake.return_val = -ENODEV;
	zassert_equal(camera_init(), -ENODEV);
}
