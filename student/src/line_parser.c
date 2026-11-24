/* Montador de linhas de UART. Veja line_parser.h. */
#include "line_parser.h"

#include <string.h>

void line_parser_init(struct line_parser *lp, enum line_ending ending)
{
	memset(lp, 0, sizeof(*lp));
	lp->ending = ending;
}

/** Volta ao estado "nenhuma linha começada". */
static void restart(struct line_parser *lp)
{
	lp->len = 0;
	lp->bad = false;
	lp->discarding = false;
}

/** Fecha a linha atual no LF: devolve o comprimento do corpo ou -1. */
static int finish(struct line_parser *lp)
{
	size_t body = lp->len;
	bool ok = !lp->discarding && !lp->bad;

	if (lp->ending == LINE_CRLF) {
		/* O último byte antes do LF precisa ser o CR (e é o único CR). */
		ok = ok && body > 0U && lp->buf[body - 1U] == '\r';
		if (ok) {
			body--; /* tira o CR */
		}
	}
	ok = ok && body > 0U; /* linha vazia não é um quadro */

	lp->buf[body] = '\0';
	restart(lp);
	return ok ? (int)body : -1;
}

int line_parser_feed(struct line_parser *lp, uint8_t byte, int64_t now_ms)
{
	if (byte == (uint8_t)'\n') {
		return finish(lp);
	}
	if (lp->discarding) {
		return -1; /* jogando fora até o LF */
	}
	if (lp->len == 0U) {
		lp->first_ms = now_ms; /* começa o prazo de montagem */
	}
	/* Corpo + LF não pode passar de UART_FRAME_MAX bytes. */
	if (lp->len + 1U >= UART_FRAME_MAX) {
		lp->discarding = true;
		return -1;
	}

	bool printable = byte >= 0x20U && byte <= 0x7eU;
	bool cr_allowed = lp->ending == LINE_CRLF && byte == (uint8_t)'\r';

	/*
	 * Na câmera o CR só é aceito como penúltimo byte. Um CR no meio do
	 * corpo deixa um byte depois dele, e então o corpo fica inválido.
	 */
	if (lp->len > 0U && lp->buf[lp->len - 1U] == '\r') {
		lp->bad = true;
	}
	if (!printable && !cr_allowed) {
		lp->bad = true;
	}
	lp->buf[lp->len] = (char)byte;
	lp->len++;
	return -1;
}

void line_parser_poll(struct line_parser *lp, int64_t now_ms)
{
	if (lp->len > 0U && !lp->discarding &&
	    now_ms - lp->first_ms >= UART_ASSEMBLY_MS) {
		lp->discarding = true;
	}
}
