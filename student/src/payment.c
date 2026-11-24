/*
 * Protocolo P1 (REQ-05, REQ-11). Ver payment.h.
 */
#include "payment.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "camera.h"
#include "line_parser.h"

int payment_format_charge(char *out, size_t size, const char *uuid, const char *plate,
			  uint32_t amount)
{
	int n;

	if (!payment_uuid_valid(uuid) || !camera_plate_valid(plate) ||
	    amount < PAYMENT_AMOUNT_MIN || amount > PAYMENT_AMOUNT_MAX) {
		return -EINVAL;
	}
	n = snprintf(out, size, "P1 CHARGE %s %s %u BRL\n", uuid, plate, (unsigned int)amount);
	/* n negativo (erro) vira um valor enorme no cast e também cai aqui. */
	if ((size_t)n >= size) {
		return -EINVAL;
	}
	return n;
}

int payment_format_cancel(char *out, size_t size, const char *uuid)
{
	int n;

	if (!payment_uuid_valid(uuid)) {
		return -EINVAL;
	}
	n = snprintf(out, size, "P1 CANCEL %s\n", uuid);
	/* n negativo (erro) vira um valor enorme no cast e também cai aqui. */
	if ((size_t)n >= size) {
		return -EINVAL;
	}
	return n;
}

static bool is_hex_lower(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

bool payment_uuid_valid(const char *uuid)
{
	for (int i = 0; i < UUID_LEN; i++) {
		bool dash = i == 8 || i == 13 || i == 18 || i == 23;

		if (dash ? uuid[i] != '-' : !is_hex_lower(uuid[i])) {
			return false;
		}
	}
	return uuid[UUID_LEN] == '\0' || uuid[UUID_LEN] == ' ';
}

/* Interpreta "<uuid> <PAID|CANCELLED> <valor> BRL". */
static bool parse_result(const char *s, struct payment_msg *m)
{
	static const char paid[] = "PAID ";
	static const char cancelled[] = "CANCELLED ";
	uint32_t amount;

	/* payment_uuid_valid já para no primeiro caractere fora do formato. */
	if (!payment_uuid_valid(s) || s[UUID_LEN] != ' ') {
		return false;
	}
	memcpy(m->uuid, s, UUID_LEN);
	m->uuid[UUID_LEN] = '\0';
	s += UUID_LEN + 1;

	if (strncmp(s, paid, sizeof(paid) - 1) == 0) {
		m->kind = PAYMENT_PAID;
		s += sizeof(paid) - 1;
	} else if (strncmp(s, cancelled, sizeof(cancelled) - 1) == 0) {
		m->kind = PAYMENT_CANCELLED;
		s += sizeof(cancelled) - 1;
	} else {
		return false;
	}
	if (!lp_parse_uint(&s, PAYMENT_AMOUNT_MAX, &amount) || amount < PAYMENT_AMOUNT_MIN) {
		return false;
	}
	if (strcmp(s, " BRL") != 0) {
		return false;
	}
	m->amount = amount;
	return true;
}

void payment_parse(const char *line, struct payment_msg *m)
{
	static const char result[] = "P1 RESULT ";
	static const char *const errors[] = {
		"P1 ERROR FRAME", "P1 ERROR TIMEOUT", "P1 ERROR CAPACITY",
		"P1 ERROR CONFLICT", "P1 ERROR UNKNOWN",
	};

	m->kind = PAYMENT_INVALID;
	if (strncmp(line, result, sizeof(result) - 1) == 0) {
		if (!parse_result(line + sizeof(result) - 1, m)) {
			m->kind = PAYMENT_INVALID;
		}
		return;
	}
	for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
		if (strcmp(line, errors[i]) == 0) {
			m->kind = PAYMENT_ERROR;
			return;
		}
	}
}

/*
 * Gerador xorshift64*: não é criptográfico, só precisa que cada tentativa de
 * saída tenha um UUID diferente dentro de uma execução. O contador garante
 * entradas distintas; a entropia (ciclos da CPU) varia entre execuções.
 */
static uint64_t rng_state = 0x9E3779B97F4A7C15ULL;
static uint32_t uuid_counter;

static uint64_t rng_next(void)
{
	rng_state ^= rng_state >> 12;
	rng_state ^= rng_state << 25;
	rng_state ^= rng_state >> 27;
	return rng_state * 0x2545F4914F6CDD1DULL;
}

void payment_new_uuid(char out[UUID_BUF], uint32_t entropy)
{
	uint8_t b[16];
	uint64_t hi;
	uint64_t lo;
	int pos = 0;

	uuid_counter++;
	rng_state ^= ((uint64_t)entropy << 32) | uuid_counter;
	hi = rng_next();
	lo = rng_next();
	for (int i = 0; i < 8; i++) {
		b[i] = (uint8_t)(hi >> (56 - 8 * i));
		b[8 + i] = (uint8_t)(lo >> (56 - 8 * i));
	}
	b[6] = (uint8_t)((b[6] & 0x0FU) | 0x40U); /* versão 4 */
	b[8] = (uint8_t)((b[8] & 0x3FU) | 0x80U); /* variante RFC 4122 */

	for (int i = 0; i < 16; i++) {
		static const char hex[] = "0123456789abcdef";

		if (i == 4 || i == 6 || i == 8 || i == 10) {
			out[pos++] = '-';
		}
		out[pos++] = hex[b[i] >> 4];
		out[pos++] = hex[b[i] & 0x0FU];
	}
	out[pos] = '\0';
}
