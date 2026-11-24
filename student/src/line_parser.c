/*
 * Montador de linhas limitado (REQ-11). Ver line_parser.h.
 */
#include "line_parser.h"

void lp_init(struct line_parser *p, bool crlf)
{
	p->len = 0;
	p->discarding = false;
	p->crlf = crlf;
	p->start_ms = 0;
	p->buf[0] = '\0';
}

/* Passa a descartar até o próximo LF. */
static enum lp_result drop(struct line_parser *p)
{
	p->len = 0;
	p->discarding = true;
	return LP_DROPPED;
}

/* Confere o quadro completo (sem o LF) e deixa só o corpo em buf. */
static bool finish(struct line_parser *p)
{
	size_t body = p->len;

	if (p->crlf) {
		if (body == 0 || p->buf[body - 1] != '\r') {
			return false;
		}
		body--;
	}
	if (body == 0) {
		return false;
	}
	for (size_t i = 0; i < body; i++) {
		char c = p->buf[i];

		/* Só ASCII imprimível; isso também recusa um CR fora do lugar. */
		if (c < 0x20 || c > 0x7E) {
			return false;
		}
	}
	p->buf[body] = '\0';
	return true;
}

enum lp_result lp_feed(struct line_parser *p, uint8_t byte, int64_t now_ms)
{
	if (p->discarding) {
		if (byte == '\n') {
			p->discarding = false;
		}
		return LP_NONE;
	}

	if (p->len == 0) {
		p->start_ms = now_ms;
	} else if (now_ms - p->start_ms >= UART_ASSEMBLY_MS) {
		/* Montagem parcial passou do prazo: descarta até o LF. */
		enum lp_result r = drop(p);

		if (byte == '\n') {
			p->discarding = false;
		}
		return r;
	}

	if (byte == '\n') {
		bool ok = finish(p);

		p->len = 0;
		return ok ? LP_LINE : LP_DROPPED;
	}

	/* O LF ainda precisa caber no quadro de UART_FRAME_MAX bytes. */
	if (p->len >= UART_FRAME_MAX - 1) {
		return drop(p);
	}
	p->buf[p->len++] = (char)byte;
	return LP_NONE;
}

const char *lp_line(const struct line_parser *p)
{
	return p->buf;
}

bool lp_parse_uint(const char **s, uint32_t max, uint32_t *out)
{
	const char *c = *s;
	uint64_t value = 0;
	size_t digits = 0;

	while (*c >= '0' && *c <= '9') {
		value = value * 10U + (uint64_t)(*c - '0');
		digits++;
		if (value > max) {
			return false;
		}
		c++;
	}
	if (digits == 0 || (digits > 1 && **s == '0')) {
		return false;
	}
	*out = (uint32_t)value;
	*s = c;
	return true;
}
