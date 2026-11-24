/**
 * @file line.c
 * @brief Montagem limitada de quadros de texto recebidos por UART.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "line.h"

/** @brief Primeiro e último caracteres ASCII imprimíveis aceitos num quadro. */
#define ASCII_FIRST_PRINTABLE 0x20
#define ASCII_LAST_PRINTABLE  0x7e

/**
 * @brief Volta ao estado de espera de um quadro novo.
 *
 * @param f Montador.
 */
static void line_restart(struct line_framer *f)
{
	f->len = 0U;
	f->started = false;
	f->discarding = false;
}

/**
 * @brief Passa a descartar bytes até o próximo LF.
 *
 * @param f Montador.
 */
static void line_discard(struct line_framer *f)
{
	f->len = 0U;
	f->discarding = true;
}

/**
 * @brief Fecha o quadro no LF, conferindo o terminador exigido.
 *
 * @param f Montador.
 * @return LINE_READY com o corpo em buf, ou LINE_DROPPED.
 */
static enum line_result line_close(struct line_framer *f)
{
	size_t body = f->len;

	if (f->crlf) {
		if ((body == 0U) || (f->buf[body - 1U] != '\r')) {
			line_restart(f);
			return LINE_DROPPED;
		}
		body--;
	}

	if (body == 0U) {
		line_restart(f);
		return LINE_DROPPED;
	}

	f->buf[body] = '\0';
	f->len = body;
	f->started = false;

	return LINE_READY;
}

void line_init(struct line_framer *f, bool crlf)
{
	f->crlf = crlf;
	f->first_byte_ms = 0;
	f->buf[0] = '\0';
	line_restart(f);
}

enum line_result line_feed(struct line_framer *f, uint8_t byte, int64_t now_ms)
{
	if (!f->started) {
		f->started = true;
		f->len = 0U;
		f->first_byte_ms = now_ms;
	} else if (!f->discarding && ((now_ms - f->first_byte_ms) > LINE_ASSEMBLY_MS)) {
		line_discard(f);
	}

	if (f->discarding) {
		if (byte == '\n') {
			line_restart(f);
			return LINE_DROPPED;
		}
		return LINE_NONE;
	}

	if (byte == '\n') {
		return line_close(f);
	}

	/* No camera o CR só pode vir imediatamente antes do LF. */
	if ((f->len > 0U) && (f->buf[f->len - 1U] == '\r')) {
		line_discard(f);
		return LINE_NONE;
	}

	if (byte == '\r') {
		if (!f->crlf) {
			line_discard(f);
			return LINE_NONE;
		}
	} else if ((byte < ASCII_FIRST_PRINTABLE) || (byte > ASCII_LAST_PRINTABLE)) {
		line_discard(f);
		return LINE_NONE;
	}

	/* Reserva espaço para o LF: o quadro inteiro cabe em LINE_MAX_FRAME. */
	if (f->len >= (LINE_MAX_FRAME - 1U)) {
		line_discard(f);
		return LINE_NONE;
	}

	f->buf[f->len] = (char)byte;
	f->len++;

	return LINE_NONE;
}

bool line_expire(struct line_framer *f, int64_t now_ms)
{
	if (!f->started || f->discarding) {
		return false;
	}

	if ((now_ms - f->first_byte_ms) <= LINE_ASSEMBLY_MS) {
		return false;
	}

	line_discard(f);

	return true;
}

const char *line_frame(const struct line_framer *f)
{
	return f->buf;
}
