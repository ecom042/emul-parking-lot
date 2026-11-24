/**
 * @file payment.c
 * @brief Cobrança no terminal de pagamento pelo protocolo P1 (LPUART1).
 *
 * Os quadros da cobrança são montados uma vez, em payment_start(): o reenvio
 * manda os mesmos bytes, e uma resposta só vale se for idêntica, caractere a
 * caractere, ao PAID ou ao CANCELLED esperados para o UUID e o valor atuais.
 */

#include "payment.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/toolchain.h>

#include "hw.h"
#include "line.h"

/** @brief Estado da cobrança. */
struct payment {
	struct line_framer rx;             /**< Montador dos quadros recebidos (só LF). */
	bool active;                       /**< Cobrança aguardando resultado ou liberação da faixa. */
	bool paid_received;                 /**< PAID atual, esperando autorização da faixa. */
	unsigned int sends;                /**< Envios do CHARGE atual. */
	int64_t sent_ms;                   /**< Instante do último envio. */
	struct payment_stats stats;        /**< Contadores de diagnóstico. */
	char uuid[PAYMENT_UUID_LEN + 1];   /**< UUID da cobrança atual. */
	char charge[LINE_MAX_FRAME];       /**< Quadro CHARGE, com o LF. */
	char cancel[LINE_MAX_FRAME];       /**< Quadro CANCEL, com o LF. */
	char paid[LINE_MAX_FRAME];         /**< Resposta PAID esperada, sem o LF. */
	char cancelled[LINE_MAX_FRAME];    /**< Resposta CANCELLED esperada, sem o LF. */
};

static struct payment pay;

/** @brief Último número codificado num UUID; payment_init() não o zera. */
static uint32_t uuid_counter;

/**
 * @brief Formata um texto e acumula se ele não coube no destino.
 *
 * @param buf Destino.
 * @param size Tamanho do destino.
 * @param truncated Passa a true se o texto não coube; nunca volta a false.
 * @param fmt Formato de printf.
 */
static __printf_like(4, 5) void put(char *buf, size_t size, bool *truncated, const char *fmt,
				    ...)
{
	va_list ap;
	int len;

	va_start(ap, fmt);
	len = vsnprintf(buf, size, fmt, ap);
	va_end(ap);
	/* Um erro (len < 0) vira um size_t enorme e também conta como truncado. */
	*truncated |= (size_t)len >= size;
}

/**
 * @brief Envia um quadro pronto pela UART do terminal.
 *
 * @param frame Quadro terminado em LF.
 * @return 0 em sucesso ou o erro de hw_uart_write().
 */
static int send_frame(const char *frame)
{
	return hw_uart_write(HW_UART_PAYMENT, (const uint8_t *)frame, strlen(frame));
}

/**
 * @brief Envia o CHARGE atual e reinicia o prazo de resposta.
 *
 * @param now_ms Tempo virtual atual.
 * @return 0 em sucesso ou o erro de hw_uart_write().
 */
static int send_charge(int64_t now_ms)
{
	int err = send_frame(pay.charge);

	if (err < 0) {
		return err;
	}
	pay.sends++;
	pay.sent_ms = now_ms;
	return 0;
}

/**
 * @brief Classifica um quadro completo recebido durante a cobrança.
 *
 * @param frame Quadro sem o LF.
 * @return PAID ou FAILED se o quadro é exatamente uma das respostas esperadas,
 *         senão PAYMENT_EV_NONE.
 */
static enum payment_event classify(const char *frame)
{
	if (strcmp(frame, pay.paid) == 0) {
		pay.paid_received = true;
		return PAYMENT_EV_PAID;
	}
	if (strcmp(frame, pay.cancelled) == 0) {
		if (payment_abort() < 0) {
			pay.stats.cancel_failures++;
		}
		return PAYMENT_EV_FAILED;
	}
	return PAYMENT_EV_NONE;
}

/**
 * @brief Entrega um byte recebido ao montador e trata o quadro que ele completar.
 *
 * @param byte Byte recebido.
 * @param now_ms Tempo virtual atual.
 * @return Desfecho da cobrança, se o byte completou uma resposta válida.
 */
static enum payment_event receive(uint8_t byte, int64_t now_ms)
{
	switch (line_feed(&pay.rx, byte, now_ms)) {
	case LINE_READY:
		return pay.active ? classify(line_frame(&pay.rx)) : PAYMENT_EV_NONE;
	case LINE_DROPPED:
		pay.stats.dropped++;
		break;
	case LINE_NONE:
		break;
	}
	return PAYMENT_EV_NONE;
}

/**
 * @brief Reenvia a cobrança no fim do prazo ou desiste quando os envios acabam.
 *
 * @param now_ms Tempo virtual atual.
 * @return PAYMENT_EV_FAILED ao desistir, senão PAYMENT_EV_NONE.
 */
static enum payment_event check_deadline(int64_t now_ms)
{
	if (!pay.active || now_ms - pay.sent_ms < PAYMENT_TIMEOUT_MS) {
		return PAYMENT_EV_NONE;
	}
	if (pay.sends < PAYMENT_MAX_SENDS && send_charge(now_ms) == 0) {
		return PAYMENT_EV_NONE;
	}
	if (payment_abort() < 0) {
		pay.stats.cancel_failures++;
	}
	return PAYMENT_EV_FAILED;
}

int payment_init(void)
{
	memset(&pay, 0, sizeof(pay));
	line_init(&pay.rx, false);
	return 0;
}

int payment_start(const char *plate, uint32_t amount, int64_t now_ms)
{
	bool truncated = false;
	int err;

	if (plate == NULL || strnlen(plate, PAYMENT_PLATE_LEN + 1) != PAYMENT_PLATE_LEN ||
	    amount == 0U || amount > PAYMENT_MAX_AMOUNT) {
		return -EINVAL;
	}
	if (pay.active) {
		return -EBUSY;
	}
	uuid_counter++;
	put(pay.uuid, sizeof(pay.uuid), &truncated, "00000000-0000-4000-8000-%012" PRIx32,
	    uuid_counter);
	put(pay.charge, sizeof(pay.charge), &truncated, "P1 CHARGE %s %s %" PRIu32 " BRL\n",
	    pay.uuid, plate, amount);
	put(pay.cancel, sizeof(pay.cancel), &truncated, "P1 CANCEL %s\n", pay.uuid);
	put(pay.paid, sizeof(pay.paid), &truncated, "P1 RESULT %s PAID %" PRIu32 " BRL", pay.uuid,
	    amount);
	put(pay.cancelled, sizeof(pay.cancelled), &truncated,
	    "P1 RESULT %s CANCELLED %" PRIu32 " BRL", pay.uuid, amount);
	if (truncated) {
		return -ENOSPC;
	}
	pay.sends = 0U;
	pay.paid_received = false;
	err = send_charge(now_ms);
	if (err < 0) {
		return err;
	}
	pay.active = true;
	return 0;
}

/** @brief Entrega PAID uma vez, assim que a faixa puder usá-lo. */
static enum payment_event authorize_paid(bool authorize)
{
	if (!authorize) {
		return PAYMENT_EV_NONE;
	}
	pay.active = false;
	return PAYMENT_EV_PAID;
}

enum payment_event payment_poll(int64_t now_ms, bool authorize)
{
	enum payment_event event;
	uint8_t byte;
	size_t remaining = LINE_MAX_FRAME;

	if (pay.active && pay.paid_received) {
		return authorize_paid(authorize);
	}
	event = check_deadline(now_ms);
	if (event == PAYMENT_EV_FAILED) {
		return event;
	}
	if (line_expire(&pay.rx, now_ms)) {
		pay.stats.expired++;
	}
	while (remaining-- > 0 && hw_uart_read(HW_UART_PAYMENT, &byte) == 0) {
		event = receive(byte, now_ms);
		if (event == PAYMENT_EV_PAID) {
			return authorize_paid(authorize);
		}
		if (event == PAYMENT_EV_FAILED) {
			return event;
		}
	}
	return PAYMENT_EV_NONE;
}

int payment_abort(void)
{
	if (!pay.active) {
		return 0;
	}
	pay.active = false;
	return send_frame(pay.cancel);
}

int payment_active(void)
{
	return pay.active ? 1 : 0;
}

void payment_stats_get(struct payment_stats *stats)
{
	*stats = pay.stats;
}
