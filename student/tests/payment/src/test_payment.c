/**
 * @file test_payment.c
 * @brief Testes da cobrança P1 (payment.c) com a UART falsa do FFF.
 *
 * O montador de quadros (line.c) é o real. hw_uart_write() guarda cada quadro
 * enviado; hw_uart_read() entrega os bytes de uma resposta, opcionalmente
 * poucos por chamada de payment_poll(), como os fragmentos do terminal.
 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "hw.h"
#include "payment.h"

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, hw_uart_write, enum hw_uart, const uint8_t *, size_t);
FAKE_VALUE_FUNC(int, hw_uart_read, enum hw_uart, uint8_t *);

#define PLATE  "ABC1D23"
#define AMOUNT 400U
#define T0     10000
#define MAX_TX 8

/** @brief Mundo simulado lido e escrito pelos falsos. */
static struct {
	char tx[MAX_TX][128];    /**< Quadros enviados, na ordem. */
	unsigned int tx_count;   /**< Quantidade de quadros enviados. */
	unsigned int write_fail; /**< Chamada de escrita (1, 2, ...) a partir da qual falha; 0 nunca. */
	const char *rx;          /**< Bytes ainda não lidos da resposta. */
	int rx_budget;           /**< Bytes que ainda podem ser lidos neste poll; < 0 sem limite. */
	bool wrong_uart;         /**< Alguma chamada usou outra UART. */
} w;

/**
 * @brief Falso de hw_uart_write(): guarda o quadro ou falha a partir de write_fail.
 *
 * @param uart UART de destino.
 * @param data Bytes.
 * @param len Quantidade de bytes.
 * @return 0 ou -EIO.
 */
static int fake_write(enum hw_uart uart, const uint8_t *data, size_t len)
{
	w.wrong_uart |= uart != HW_UART_PAYMENT;
	if (w.write_fail != 0U && hw_uart_write_fake.call_count >= w.write_fail) {
		return -EIO;
	}
	zassert_true(w.tx_count < MAX_TX);
	zassert_true(len < sizeof(w.tx[0]));
	memcpy(w.tx[w.tx_count], data, len);
	w.tx[w.tx_count][len] = '\0';
	w.tx_count++;
	return 0;
}

/**
 * @brief Falso de hw_uart_read(): entrega o próximo byte da resposta.
 *
 * @param uart UART de origem.
 * @param byte Recebe o byte.
 * @return 0 ou -EAGAIN.
 */
static int fake_read(enum hw_uart uart, uint8_t *byte)
{
	w.wrong_uart |= uart != HW_UART_PAYMENT;
	if (w.rx == NULL || *w.rx == '\0' || w.rx_budget == 0) {
		return -EAGAIN;
	}
	if (w.rx_budget > 0) {
		w.rx_budget--;
	}
	*byte = (uint8_t)*w.rx++;
	return 0;
}

/**
 * @brief Restaura falsos, mundo e módulo antes de cada teste.
 *
 * @param fixture Não usado.
 */
static void reset_world(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(hw_uart_write);
	RESET_FAKE(hw_uart_read);
	FFF_RESET_HISTORY();
	hw_uart_write_fake.custom_fake = fake_write;
	hw_uart_read_fake.custom_fake = fake_read;
	memset(&w, 0, sizeof(w));
	w.rx_budget = -1;
	zassert_ok(payment_init());
}

/**
 * @brief Confere, depois de cada teste, que só a UART do terminal foi usada.
 *
 * @param fixture Não usado.
 */
static void check_uart(void *fixture)
{
	ARG_UNUSED(fixture);
	zassert_false(w.wrong_uart, "só a UART do terminal pode ser usada");
}

ZTEST_SUITE(payment, NULL, NULL, reset_world, check_uart, NULL);

/**
 * @brief snprintf() que reprova o teste se o texto não couber.
 *
 * @param buf Destino.
 * @param size Tamanho do destino.
 * @param fmt Formato de printf.
 */
static __printf_like(3, 4) void format(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	zassert_true(len >= 0 && (size_t)len < size, "texto não coube");
}

/**
 * @brief Extrai o UUID do primeiro CHARGE enviado.
 *
 * @param uuid Recebe o UUID, com NUL.
 */
static void sent_uuid(char uuid[PAYMENT_UUID_LEN + 1])
{
	zassert_true(w.tx_count > 0);
	zassert_ok(strncmp(w.tx[0], "P1 CHARGE ", 10));
	memcpy(uuid, w.tx[0] + 10, PAYMENT_UUID_LEN);
	uuid[PAYMENT_UUID_LEN] = '\0';
}

/**
 * @brief Monta uma resposta RESULT com o UUID, o resultado e o valor dados.
 *
 * @param buf Destino.
 * @param size Tamanho do destino.
 * @param uuid UUID.
 * @param result PAID ou CANCELLED.
 * @param amount Valor em texto.
 * @return buf.
 */
static const char *result(char *buf, size_t size, const char *uuid, const char *result,
			  const char *amount)
{
	format(buf, size, "P1 RESULT %s %s %s BRL\n", uuid, result, amount);
	return buf;
}

/**
 * @brief Começa a cobrança padrão em T0 e devolve o UUID enviado.
 *
 * @param uuid Recebe o UUID.
 */
static void start(char uuid[PAYMENT_UUID_LEN + 1])
{
	zassert_ok(payment_start(PLATE, AMOUNT, T0));
	zassert_equal(payment_active(), 1);
	sent_uuid(uuid);
}

ZTEST(payment, test_happy_path)
{
	char uuid[PAYMENT_UUID_LEN + 1];
	char expected[128];
	char reply[128];

	start(uuid);
	zassert_equal(w.tx_count, 1);
	format(expected, sizeof(expected), "P1 CHARGE %s " PLATE " 400 BRL\n", uuid);
	zassert_str_equal(w.tx[0], expected);

	zassert_equal(payment_poll(T0 + 50, true), PAYMENT_EV_NONE, "nada chegou ainda");
	w.rx = result(reply, sizeof(reply), uuid, "PAID", "400");
	zassert_equal(payment_poll(T0 + 100, true), PAYMENT_EV_PAID);
	zassert_equal(payment_active(), 0);
	zassert_equal(payment_poll(T0 + 5000, true), PAYMENT_EV_NONE, "sem cobrança, sem prazo");
	zassert_equal(w.tx_count, 1, "PAID não envia nada");
}

ZTEST(payment, test_uuid_format)
{
	char uuid[PAYMENT_UUID_LEN + 1];

	start(uuid);
	zassert_equal(strlen(uuid), 36);
	for (int i = 0; i < PAYMENT_UUID_LEN; i++) {
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			zassert_equal(uuid[i], '-', "hífen na posição %d", i);
		} else {
			zassert_true(isxdigit((unsigned char)uuid[i]) && !isupper((unsigned char)uuid[i]),
				     "hexadecimal minúsculo na posição %d", i);
		}
	}
}

ZTEST(payment, test_uuid_unique_since_boot)
{
	char seen[6][PAYMENT_UUID_LEN + 1];

	for (int i = 0; i < 6; i++) {
		if (i == 3) {
			zassert_ok(payment_init(), "init não pode reiniciar os UUIDs");
		}
		w.tx_count = 0;
		start(seen[i]);
		zassert_ok(payment_abort());
		for (int j = 0; j < i; j++) {
			zassert_true(strcmp(seen[i], seen[j]) != 0, "UUID %d repetiu o %d", i, j);
		}
	}
}

ZTEST(payment, test_paid_in_fragments)
{
	char uuid[PAYMENT_UUID_LEN + 1];
	char reply[128];
	int64_t now = T0 + 100;

	start(uuid);
	w.rx = result(reply, sizeof(reply), uuid, "PAID", "400");
	while (strlen(w.rx) > 2) {
		w.rx_budget = 2;
		zassert_equal(payment_poll(now, true), PAYMENT_EV_NONE, "quadro ainda incompleto");
		now += 10;
	}
	w.rx_budget = 2;
	zassert_equal(payment_poll(now, true), PAYMENT_EV_PAID);
}

ZTEST(payment, test_resend_same_charge_at_most_three_times)
{
	char uuid[PAYMENT_UUID_LEN + 1];

	start(uuid);
	zassert_equal(payment_poll(T0 + PAYMENT_TIMEOUT_MS - 1, true), PAYMENT_EV_NONE);
	zassert_equal(w.tx_count, 1, "ainda no prazo");
	zassert_equal(payment_poll(T0 + PAYMENT_TIMEOUT_MS, true), PAYMENT_EV_NONE);
	zassert_equal(w.tx_count, 2);
	zassert_equal(payment_poll(T0 + 2 * PAYMENT_TIMEOUT_MS - 1, true), PAYMENT_EV_NONE);
	zassert_equal(w.tx_count, 2, "o prazo conta desde o último envio");
	zassert_equal(payment_poll(T0 + 2 * PAYMENT_TIMEOUT_MS, true), PAYMENT_EV_NONE);
	zassert_equal(w.tx_count, PAYMENT_MAX_SENDS);
	for (unsigned int i = 1; i < PAYMENT_MAX_SENDS; i++) {
		zassert_str_equal(w.tx[i], w.tx[0], "reenvio %u igual ao primeiro", i);
	}
}

ZTEST(payment, test_timeout_after_three_sends_cancels)
{
	char uuid[PAYMENT_UUID_LEN + 1];
	char cancel[64];
	char reply[128];
	struct payment_stats stats;

	start(uuid);
	zassert_equal(payment_poll(T0 + PAYMENT_TIMEOUT_MS, true), PAYMENT_EV_NONE);
	zassert_equal(payment_poll(T0 + 2 * PAYMENT_TIMEOUT_MS, true), PAYMENT_EV_NONE);
	zassert_equal(payment_poll(T0 + 3 * PAYMENT_TIMEOUT_MS, true), PAYMENT_EV_FAILED);
	zassert_equal(payment_active(), 0);
	zassert_equal(w.tx_count, PAYMENT_MAX_SENDS + 1);
	format(cancel, sizeof(cancel), "P1 CANCEL %s\n", uuid);
	zassert_str_equal(w.tx[PAYMENT_MAX_SENDS], cancel);

	w.rx = result(reply, sizeof(reply), uuid, "PAID", "400");
	zassert_equal(payment_poll(T0 + 3 * PAYMENT_TIMEOUT_MS + 10, true), PAYMENT_EV_NONE,
		      "PAID depois de desistir não vale");
	zassert_equal(payment_poll(T0 + 10 * PAYMENT_TIMEOUT_MS, true), PAYMENT_EV_NONE);
	zassert_equal(w.tx_count, PAYMENT_MAX_SENDS + 1, "nada mais é enviado");
	payment_stats_get(&stats);
	zassert_equal(stats.cancel_failures, 0);
}

ZTEST(payment, test_cancelled_invalidates_then_confirms_cancel)
{
	char uuid[PAYMENT_UUID_LEN + 1];
	char reply[128];

	start(uuid);
	w.rx = result(reply, sizeof(reply), uuid, "CANCELLED", "400");
	zassert_equal(payment_poll(T0 + 100, true), PAYMENT_EV_FAILED);
	zassert_equal(payment_active(), 0);
	zassert_equal(w.tx_count, 2);
	zassert_ok(strncmp(w.tx[1], "P1 CANCEL ", 10));
	zassert_ok(payment_abort(), "abort sem cobrança ativa não faz nada");
	zassert_equal(w.tx_count, 2);
}

ZTEST(payment, test_stale_uuid_is_ignored)
{
	char old_uuid[PAYMENT_UUID_LEN + 1];
	char uuid[PAYMENT_UUID_LEN + 1];
	char reply[128];

	start(old_uuid);
	zassert_ok(payment_abort());
	w.tx_count = 0;
	start(uuid);
	zassert_true(strcmp(uuid, old_uuid) != 0);

	w.rx = result(reply, sizeof(reply), old_uuid, "PAID", "400");
	zassert_equal(payment_poll(T0 + 100, true), PAYMENT_EV_NONE, "PAID do carro anterior");
	zassert_equal(payment_active(), 1);
	w.rx = result(reply, sizeof(reply), uuid, "PAID", "400");
	zassert_equal(payment_poll(T0 + 200, true), PAYMENT_EV_PAID);
}

ZTEST(payment, test_other_amount_is_ignored_and_keeps_deadline)
{
	char uuid[PAYMENT_UUID_LEN + 1];
	char reply[128];

	start(uuid);
	w.rx = result(reply, sizeof(reply), uuid, "PAID", "500");
	zassert_equal(payment_poll(T0 + 500, true), PAYMENT_EV_NONE);
	zassert_equal(payment_active(), 1);
	zassert_equal(payment_poll(T0 + PAYMENT_TIMEOUT_MS, true), PAYMENT_EV_NONE);
	zassert_equal(w.tx_count, 2, "resposta errada não reinicia o prazo");
}

ZTEST(payment, test_malformed_replies_are_ignored)
{
	char uuid[PAYMENT_UUID_LEN + 1];
	static char frames[12][200];
	char reply[128];
	struct payment_stats stats;
	int n = 0;

	start(uuid);
	format(frames[n++], sizeof(frames[0]), "P1 RESULT %s PAID broken BRL\n", uuid);
	format(frames[n++], sizeof(frames[0]), "P1 RESULT %s PAID 0400 BRL\n", uuid);
	format(frames[n++], sizeof(frames[0]), "P1 RESULT %s PAID +400 BRL\n", uuid);
	format(frames[n++], sizeof(frames[0]), "P1 RESULT %s PAID 4.00 BRL\n", uuid);
	format(frames[n++], sizeof(frames[0]), "P1 RESULT %s  PAID 400 BRL\n", uuid);
	format(frames[n++], sizeof(frames[0]), "P1 RESULT %s PAID 400 BRL \n", uuid);
	format(frames[n++], sizeof(frames[0]), "P1 RESULT %s PAID 400 brl\n", uuid);
	format(frames[n++], sizeof(frames[0]), "P1 RESULT %.35s PAID 400 BRL\n", uuid);
	format(frames[n++], sizeof(frames[0]), "P1 RESULT %s paid 400 BRL\n", uuid);
	format(frames[n++], sizeof(frames[0]), "P1 ERROR TIMEOUT\n");
	format(frames[n++], sizeof(frames[0]), "P1 RESULT %s PAID 400 BRL\r\n", uuid);
	memset(frames[n], 'P', 150);
	frames[n][150] = '\n';
	frames[n][151] = '\0';
	n++;

	for (int i = 0; i < n; i++) {
		w.rx = frames[i];
		while (*w.rx != '\0') {
			zassert_equal(payment_poll(T0 + 10 + i, true), PAYMENT_EV_NONE,
				      "quadro %d autorizou", i);
		}
		zassert_equal(payment_active(), 1);
	}
	payment_stats_get(&stats);
	zassert_equal(stats.dropped, 2, "CR e quadro longo são descartados no LF");
	zassert_equal(w.tx_count, 1, "nenhum quadro reinicia o prazo nem reenvia");

	w.rx = result(reply, sizeof(reply), uuid, "PAID", "400");
	zassert_equal(payment_poll(T0 + 900, true), PAYMENT_EV_PAID, "o próximo quadro válido vale");
}

ZTEST(payment, test_duplicate_paid_has_no_effect)
{
	char uuid[PAYMENT_UUID_LEN + 1];
	char twice[256];
	char reply[128];

	start(uuid);
	result(reply, sizeof(reply), uuid, "PAID", "400");
	format(twice, sizeof(twice), "%s%s", reply, reply);
	w.rx = twice;
	zassert_equal(payment_poll(T0 + 100, true), PAYMENT_EV_PAID);
	zassert_equal(payment_poll(T0 + 110, true), PAYMENT_EV_NONE, "segundo PAID não conta");
	zassert_equal(*w.rx, '\0', "o duplicado foi lido e descartado");
}

ZTEST(payment, test_abort_then_late_paid)
{
	char uuid[PAYMENT_UUID_LEN + 1];
	char cancel[64];
	char reply[128];

	start(uuid);
	zassert_ok(payment_abort());
	zassert_equal(payment_active(), 0, "UUID invalidado");
	zassert_equal(w.tx_count, 2);
	format(cancel, sizeof(cancel), "P1 CANCEL %s\n", uuid);
	zassert_str_equal(w.tx[1], cancel);

	w.rx = result(reply, sizeof(reply), uuid, "PAID", "400");
	zassert_equal(payment_poll(T0 + 100, true), PAYMENT_EV_NONE, "PAID atrasado depois do abort");
	zassert_equal(payment_poll(T0 + 5000, true), PAYMENT_EV_NONE);
	zassert_equal(w.tx_count, 2);
}

ZTEST(payment, test_reply_without_charge_is_ignored)
{
	char reply[128];

	w.rx = result(reply, sizeof(reply), "00000000-0000-4000-8000-000000000001", "PAID", "400");
	zassert_equal(payment_poll(T0, true), PAYMENT_EV_NONE);
	zassert_equal(payment_active(), 0);
	zassert_equal(w.tx_count, 0);
}

ZTEST(payment, test_partial_frame_expires)
{
	char uuid[PAYMENT_UUID_LEN + 1];
	char reply[128];
	struct payment_stats stats;

	start(uuid);
	result(reply, sizeof(reply), uuid, "PAID", "400");
	w.rx = reply;
	w.rx_budget = 10;
	zassert_equal(payment_poll(T0 + 100, true), PAYMENT_EV_NONE);
	w.rx_budget = 0;
	zassert_equal(payment_poll(T0 + 100 + 1000, true), PAYMENT_EV_NONE);
	payment_stats_get(&stats);
	zassert_equal(stats.expired, 0, "1000 ms ainda está no prazo");
	zassert_equal(payment_poll(T0 + 100 + 1001, true), PAYMENT_EV_NONE);
	payment_stats_get(&stats);
	zassert_equal(stats.expired, 1);

	w.rx_budget = -1;
	zassert_equal(payment_poll(T0 + 1200, true), PAYMENT_EV_NONE, "o resto vencido não autoriza");
	payment_stats_get(&stats);
	zassert_equal(stats.dropped, 1);
	zassert_equal(payment_active(), 1);

	w.rx = result(reply, sizeof(reply), uuid, "PAID", "400");
	zassert_equal(payment_poll(T0 + 1300, true), PAYMENT_EV_PAID, "o quadro seguinte ressincroniza");
}

ZTEST(payment, test_invalid_arguments_and_busy)
{
	char uuid[PAYMENT_UUID_LEN + 1];

	zassert_equal(payment_start(NULL, AMOUNT, T0), -EINVAL);
	zassert_equal(payment_start("ABC1D2", AMOUNT, T0), -EINVAL);
	zassert_equal(payment_start("ABC1D234", AMOUNT, T0), -EINVAL);
	zassert_equal(payment_start(PLATE, 0, T0), -EINVAL);
	zassert_equal(payment_start(PLATE, PAYMENT_MAX_AMOUNT + 1, T0), -EINVAL);
	zassert_equal(w.tx_count, 0);
	zassert_equal(payment_active(), 0);

	zassert_ok(payment_start(PLATE, PAYMENT_MAX_AMOUNT, T0));
	sent_uuid(uuid);
	zassert_true(strstr(w.tx[0], " 1000000 BRL\n") != NULL);
	zassert_equal(payment_start(PLATE, AMOUNT, T0), -EBUSY);
	zassert_equal(w.tx_count, 1);
}

ZTEST(payment, test_first_send_error_returns_to_caller)
{
	w.write_fail = 1;
	zassert_equal(payment_start(PLATE, AMOUNT, T0), -EIO);
	zassert_equal(payment_active(), 0, "sem envio, sem cobrança");
	zassert_ok(payment_abort());
	zassert_equal(hw_uart_write_fake.call_count, 1, "abort sem cobrança não envia");
}

ZTEST(payment, test_resend_error_fails_and_cancels)
{
	char uuid[PAYMENT_UUID_LEN + 1];
	struct payment_stats stats;

	start(uuid);
	w.write_fail = 2;
	zassert_equal(payment_poll(T0 + PAYMENT_TIMEOUT_MS, true), PAYMENT_EV_FAILED);
	zassert_equal(payment_active(), 0);
	zassert_equal(hw_uart_write_fake.call_count, 3, "reenvio falhou, depois o CANCEL");
	payment_stats_get(&stats);
	zassert_equal(stats.cancel_failures, 1, "o CANCEL também falhou");
}

ZTEST(payment, test_abort_error_returns_to_caller)
{
	char uuid[PAYMENT_UUID_LEN + 1];

	start(uuid);
	w.write_fail = 2;
	zassert_equal(payment_abort(), -EIO);
	zassert_equal(payment_active(), 0, "invalidado mesmo se o CANCEL falha");
}

ZTEST(payment, test_paid_waits_for_fresh_distance)
{
	char uuid[PAYMENT_UUID_LEN + 1];
	char frame[128];

	start(uuid);
	w.rx = result(frame, sizeof(frame), uuid, "PAID", "400");
	zassert_equal(payment_poll(T0 + 100, false), PAYMENT_EV_NONE);
	zassert_equal(payment_active(), 1);
	zassert_equal(payment_poll(T0 + 101, true), PAYMENT_EV_PAID);
	zassert_equal(payment_active(), 0);
	zassert_equal(w.tx_count, 1);
}

ZTEST(payment, test_buffered_paid_cannot_win_after_final_deadline)
{
	char uuid[PAYMENT_UUID_LEN + 1];
	char frame[128];

	start(uuid);
	zassert_equal(payment_poll(T0 + PAYMENT_TIMEOUT_MS, true), PAYMENT_EV_NONE);
	zassert_equal(payment_poll(T0 + 2 * PAYMENT_TIMEOUT_MS, true), PAYMENT_EV_NONE);
	w.rx = result(frame, sizeof(frame), uuid, "PAID", "400");
	zassert_equal(payment_poll(T0 + 3 * PAYMENT_TIMEOUT_MS + 1, true), PAYMENT_EV_FAILED);
	zassert_equal(payment_poll(T0 + 3 * PAYMENT_TIMEOUT_MS + 2, true), PAYMENT_EV_NONE);
	zassert_equal(payment_active(), 0);
	zassert_equal(w.tx_count, PAYMENT_MAX_SENDS + 1);
}

ZTEST(payment, test_retained_paid_can_be_cancelled)
{
	char uuid[PAYMENT_UUID_LEN + 1];
	char frame[128];

	start(uuid);
	w.rx = result(frame, sizeof(frame), uuid, "PAID", "400");
	zassert_equal(payment_poll(T0 + 100, false), PAYMENT_EV_NONE);
	zassert_ok(payment_abort());
	zassert_equal(payment_poll(T0 + 101, true), PAYMENT_EV_NONE);
	zassert_equal(payment_active(), 0);
	zassert_equal(w.tx_count, 2);
	zassert_ok(strncmp(w.tx[1], "P1 CANCEL ", 10));
}
