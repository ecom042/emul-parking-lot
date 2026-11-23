/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file at_parser.c
 * @brief Camera: AT+CAPTURE responde OK e depois +PLATE:<id>,"<placa>",<conf>.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "at_parser.h"

#define AT_PREFIX		"+PLATE:"
#define AT_PREFIX_LEN		7
#define AT_ID_MAX		999999999U
#define AT_CONF_MAX		100U

static void line_reset(struct at_parser *p)
{
	p->len = 0;
	p->discarding = false;
	p->have_first = false;
}

/** Decimal canonico: sem zero a esquerda, dentro da faixa. */
static bool parse_uint(const char *s, uint16_t *pos, uint16_t n,
		       uint32_t min, uint32_t max, uint32_t *out)
{
	uint16_t start = *pos;
	uint32_t value = 0;

	if (*pos >= n || s[*pos] < '0' || s[*pos] > '9') {
		return false;
	}
	if (s[*pos] == '0' && (*pos + 1U) < n &&
	    s[*pos + 1U] >= '0' && s[*pos + 1U] <= '9') {
		return false;
	}

	while (*pos < n && s[*pos] >= '0' && s[*pos] <= '9') {
		if (value > 100000000U) {
			return false;
		}
		value = value * 10U + (uint32_t)(s[*pos] - '0');
		(*pos)++;
	}
	if (*pos == start || value < min || value > max) {
		return false;
	}

	*out = value;
	return true;
}

/** Placa: [A-Z]{3}[0-9][A-Z0-9][0-9]{2}. */
static bool plate_ok(const char *s)
{
	if (!((s[0] >= 'A' && s[0] <= 'Z') && (s[1] >= 'A' && s[1] <= 'Z') &&
	      (s[2] >= 'A' && s[2] <= 'Z'))) {
		return false;
	}
	if (s[3] < '0' || s[3] > '9') {
		return false;
	}
	if (!((s[4] >= 'A' && s[4] <= 'Z') || (s[4] >= '0' && s[4] <= '9'))) {
		return false;
	}
	return (s[5] >= '0' && s[5] <= '9') && (s[6] >= '0' && s[6] <= '9');
}

static int parse_plate(const char *s, uint16_t n, struct at_plate *out)
{
	uint16_t i = AT_PREFIX_LEN;
	const char *plate;
	uint32_t id;
	uint32_t conf;

	/* prefixo, ID (1+), virgula, aspas, 7 da placa, aspas, virgula, confianca (1+) */
	if (n < (AT_PREFIX_LEN + 3U + PLATE_LEN + 3U)) {
		return 0;
	}
	if (memcmp(s, AT_PREFIX, AT_PREFIX_LEN) != 0) {
		return 0;
	}
	if (!parse_uint(s, &i, n, 1U, AT_ID_MAX, &id)) {
		return 0;
	}
	if (i + 2U >= n || s[i] != ',' || s[i + 1U] != '"') {
		return 0;
	}
	i += 2;
	plate = &s[i];
	if (i + PLATE_LEN + 3U > n || !plate_ok(plate)) {
		return 0;
	}
	i += PLATE_LEN;
	if (s[i] != '"' || s[i + 1U] != ',') {
		return 0;
	}
	i += 2;
	if (!parse_uint(s, &i, n, 0U, AT_CONF_MAX, &conf) || i != n) {
		return 0;
	}

	out->id = id;
	memcpy(out->plate, plate, PLATE_LEN);
	out->plate[PLATE_LEN] = '\0';
	out->confidence = (uint8_t)conf;
	return 1;
}

/** @return 1 para +PLATE valido; 0 para qualquer outra linha bem ou mal formada. */
static int parse_frame(const char *s, uint16_t n, struct at_plate *out)
{
	uint16_t body;

	/* A camera termina em CR LF. */
	if (n < 2U || s[n - 2U] != '\r' || s[n - 1U] != '\n') {
		return 0;
	}
	body = (uint16_t)(n - 2U);

	/* OK confirma o pedido, nao a placa; ERROR tambem nao autoriza. */
	if ((body == 2U && memcmp(s, "OK", 2) == 0) ||
	    (body == 5U && memcmp(s, "ERROR", 5) == 0)) {
		return 0;
	}
	return parse_plate(s, body, out);
}

void at_parser_reset(struct at_parser *p)
{
	if (p != NULL) {
		line_reset(p);
	}
}

int at_parser_feed(struct at_parser *p, char c, int64_t now_ms,
		   struct at_plate *out)
{
	int rc;

	if (p == NULL || out == NULL) {
		return -EINVAL;
	}

	if (!p->have_first && !p->discarding) {
		p->have_first = true;
		p->first_ms = now_ms;
	}

	/* Linha parcial com mais de 1000 ms: descarta ate o LF. */
	if (p->have_first && (now_ms - p->first_ms) > FRAME_MS) {
		p->discarding = true;
		p->len = 0;
	}

	if (p->discarding) {
		if (c == '\n') {
			line_reset(p);
		}
		return 0;
	}

	/* 128 bytes sem terminador: o quadro seguinte e o que vale. */
	if (p->len >= sizeof(p->line)) {
		p->discarding = true;
		p->len = 0;
		if (c == '\n') {
			line_reset(p);
		}
		return 0;
	}

	p->line[p->len] = c;
	p->len++;

	if (c != '\n') {
		return 0;
	}

	rc = parse_frame(p->line, p->len, out);
	line_reset(p);
	return rc;
}
