/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file p1_parser.c
 * @brief Terminal: P1 RESULT <uuid> PAID|CANCELLED <centavos> BRL e P1 ERROR <codigo>.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "p1_parser.h"

#define P1_MAX_FIELDS	6
#define P1_AMOUNT_MAX	1000000U

static void line_reset(struct p1_parser *p)
{
	p->len = 0;
	p->discarding = false;
	p->have_first = false;
}

static bool field_eq(const char *field, uint16_t len, const char *want)
{
	size_t n = strlen(want);

	return len == n && memcmp(field, want, n) == 0;
}

/** Decimal canonico: sem zero a esquerda, dentro da faixa. */
static int parse_uint(const char *s, uint16_t n, uint32_t min, uint32_t max,
		      uint32_t *out)
{
	uint32_t value = 0;
	uint16_t i = 0;

	if (n == 0 || (n > 1 && s[0] == '0')) {
		return 0;
	}
	while (i < n) {
		if (s[i] < '0' || s[i] > '9' || value > 100000000U) {
			return 0;
		}
		value = value * 10U + (uint32_t)(s[i] - '0');
		i++;
	}
	if (value < min || value > max) {
		return 0;
	}

	*out = value;
	return 1;
}

/** Hexadecimal minusculo em grupos 8-4-4-4-12. */
static bool uuid_ok(const char *s, uint16_t n)
{
	if (n != UUID_LEN) {
		return false;
	}
	for (uint16_t i = 0; i < UUID_LEN; i++) {
		bool dash = (i == 8U || i == 13U || i == 18U || i == 23U);
		char c = s[i];

		if (dash) {
			if (c != '-') {
				return false;
			}
		} else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
			return false;
		}
	}
	return true;
}

/** Separa por exatamente um espaco. @return -1 em campo vazio ou excesso. */
static int split_fields(const char *s, uint16_t n, const char *field[],
			uint16_t flen[])
{
	int count = 0;
	uint16_t start = 0;

	for (uint16_t i = 0; i <= n; i++) {
		if (i == n || s[i] == ' ') {
			if (i == start || count == P1_MAX_FIELDS) {
				return -1;
			}
			field[count] = &s[start];
			flen[count] = (uint16_t)(i - start);
			count++;
			start = (uint16_t)(i + 1U);
		}
	}
	return count;
}

static int parse_frame(const char *s, uint16_t n, struct p1_result *out)
{
	const char *field[P1_MAX_FIELDS];
	uint16_t flen[P1_MAX_FIELDS];
	uint32_t cents;
	int count;

	/* O terminal nunca termina em CR LF. */
	if (n < 1U || memchr(s, '\r', n) != NULL) {
		return 0;
	}

	count = split_fields(s, n - 1U, field, flen);
	if (count < 0 || !field_eq(field[0], flen[0], "P1")) {
		return 0;
	}

	if (count == 6 && field_eq(field[1], flen[1], "RESULT")) {
		if (!uuid_ok(field[2], flen[2]) ||
		    !parse_uint(field[4], flen[4], 1U, P1_AMOUNT_MAX, &cents) ||
		    !field_eq(field[5], flen[5], "BRL")) {
			return 0;
		}
		if (field_eq(field[3], flen[3], "PAID")) {
			out->status = P1_STATUS_PAID;
		} else if (field_eq(field[3], flen[3], "CANCELLED")) {
			out->status = P1_STATUS_CANCELLED;
		} else {
			return 0;
		}
		memcpy(out->uuid, field[2], UUID_LEN);
		out->uuid[UUID_LEN] = '\0';
		out->cents = cents;
		out->error = P1_ERROR_NONE;
		return 1;
	}

	if (count == 3 && field_eq(field[1], flen[1], "ERROR")) {
		enum p1_error error;

		if (field_eq(field[2], flen[2], "FRAME")) {
			error = P1_ERROR_FRAME;
		} else if (field_eq(field[2], flen[2], "TIMEOUT")) {
			error = P1_ERROR_TIMEOUT;
		} else if (field_eq(field[2], flen[2], "CAPACITY")) {
			error = P1_ERROR_CAPACITY;
		} else if (field_eq(field[2], flen[2], "CONFLICT")) {
			error = P1_ERROR_CONFLICT;
		} else if (field_eq(field[2], flen[2], "UNKNOWN")) {
			error = P1_ERROR_UNKNOWN;
		} else {
			return 0;
		}
		out->status = P1_STATUS_ERROR;
		out->error = error;
		out->uuid[0] = '\0';
		out->cents = 0;
		return 1;
	}

	return 0;
}

void p1_parser_reset(struct p1_parser *p)
{
	if (p != NULL) {
		line_reset(p);
	}
}

int p1_parser_feed(struct p1_parser *p, char c, int64_t now_ms,
		   struct p1_result *out)
{
	int rc;

	if (p == NULL || out == NULL) {
		return -EINVAL;
	}

	if (!p->have_first && !p->discarding) {
		p->have_first = true;
		p->first_ms = now_ms;
	}

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
