/* Mensagens da câmera e do terminal. Veja protocol.h. */
#include "protocol.h"

#include <stdio.h>
#include <string.h>

static bool is_upper(char c)
{
	return c >= 'A' && c <= 'Z';
}

static bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static bool is_lower_hex(char c)
{
	return is_digit(c) || (c >= 'a' && c <= 'f');
}

bool plate_is_valid(const char *s, size_t len)
{
	return len == PLATE_LEN && is_upper(s[0]) && is_upper(s[1]) && is_upper(s[2]) &&
	       is_digit(s[3]) && (is_upper(s[4]) || is_digit(s[4])) && is_digit(s[5]) &&
	       is_digit(s[6]);
}

bool uuid_is_valid(const char *s, size_t len)
{
	if (len != UUID_LEN) {
		return false;
	}
	for (size_t i = 0; i < UUID_LEN; i++) {
		/* Hífens exatamente nas posições 8, 13, 18 e 23. */
		bool dash_pos = i == 8U || i == 13U || i == 18U || i == 23U;
		bool ok = dash_pos ? s[i] == '-' : is_lower_hex(s[i]);

		if (!ok) {
			return false;
		}
	}
	return true;
}

bool parse_canonical_u32(const char *s, size_t len, uint32_t min, uint32_t max,
			 uint32_t *out)
{
	/* 10 algarismos já passam de qualquer valor do protocolo. */
	if (len == 0U || len > 9U || (s[0] == '0' && len > 1U)) {
		return false;
	}
	uint32_t v = 0;

	for (size_t i = 0; i < len; i++) {
		if (!is_digit(s[i])) {
			return false;
		}
		v = v * 10U + (uint32_t)(s[i] - '0');
	}
	if (v < min || v > max) {
		return false;
	}
	*out = v;
	return true;
}

/** snprintf com verificação de truncamento: devolve o comprimento ou -1. */
static int checked_len(int n, size_t size)
{
	/* n negativo (erro) vira um size_t enorme e também cai em -1. */
	return ((size_t)n < size) ? n : -1;
}

int camera_format_capture(char *buf, size_t size, uint32_t capture_id)
{
	return checked_len(snprintf(buf, size, "AT+CAPTURE=%u\r\n", (unsigned int)capture_id),
			   size);
}

int camera_parse_plate(const char *line, struct plate_result *out)
{
	static const char prefix[] = "+PLATE:";
	const size_t plen = sizeof(prefix) - 1U;

	if (strncmp(line, prefix, plen) != 0) {
		return -1;
	}
	/* Formato: <id>,"<placa>",<confiança> */
	const char *id = line + plen;
	const char *comma1 = strchr(id, ',');

	if (comma1 == NULL) {
		return -1;
	}
	const char *q1 = comma1 + 1;
	/* Aspas, 7 caracteres da placa, aspas, vírgula. */
	if (strlen(q1) < PLATE_LEN + 3U || q1[0] != '"' || q1[PLATE_LEN + 1U] != '"' ||
	    q1[PLATE_LEN + 2U] != ',') {
		return -1;
	}
	const char *plate = q1 + 1;
	const char *conf = q1 + PLATE_LEN + 3U;
	struct plate_result r;

	if (!parse_canonical_u32(id, (size_t)(comma1 - id), 1U, CAPTURE_ID_MAX,
				 &r.capture_id) ||
	    !plate_is_valid(plate, PLATE_LEN) ||
	    !parse_canonical_u32(conf, strlen(conf), 0U, 100U, &r.confidence)) {
		return -1;
	}
	memcpy(r.plate, plate, PLATE_LEN);
	r.plate[PLATE_LEN] = '\0';
	*out = r;
	return 0;
}

void uuid_make(char out[UUID_LEN + 1], uint32_t sequence, uint32_t salt)
{
	/*
	 * Estratégia: o número da tentativa de saída (contador que só cresce
	 * desde o reset) e o instante da cobrança. Dentro de uma execução o
	 * contador nunca se repete, então cada tentativa tem um UUID próprio.
	 * O reset apaga os UUIDs do terminal junto com o contador.
	 */
	(void)snprintf(out, UUID_LEN + 1U, "%08x-0000-4000-8000-%012x", (unsigned int)sequence,
		       (unsigned int)salt);
}

int payment_format_charge(char *buf, size_t size, const char *uuid, const char *plate,
			  uint32_t amount)
{
	return checked_len(snprintf(buf, size, "P1 CHARGE %s %s %u BRL\n", uuid, plate,
				    (unsigned int)amount),
			   size);
}

int payment_format_cancel(char *buf, size_t size, const char *uuid)
{
	return checked_len(snprintf(buf, size, "P1 CANCEL %s\n", uuid), size);
}

/**
 * Separa a linha em campos por UM espaço. Dois espaços seguidos geram um
 * campo vazio, que nenhum validador aceita.
 * @return número de campos, ou -1 se passar de max_fields.
 */
static int split_fields(const char *line, const char *fields[], size_t lens[],
			int max_fields)
{
	int n = 0;
	const char *start = line;

	for (const char *p = line;; p++) {
		if (*p == ' ' || *p == '\0') {
			if (n == max_fields) {
				return -1;
			}
			fields[n] = start;
			lens[n] = (size_t)(p - start);
			n++;
			if (*p == '\0') {
				return n;
			}
			start = p + 1;
		}
	}
}

static bool field_is(const char *f, size_t len, const char *word)
{
	return len == strlen(word) && strncmp(f, word, len) == 0;
}

int payment_parse_result(const char *line, struct payment_result *out)
{
	const char *f[6];
	size_t l[6];
	struct payment_result r;

	/* P1 RESULT <uuid> <status> <valor> BRL: exatamente 6 campos. */
	if (split_fields(line, f, l, 6) != 6 || !field_is(f[0], l[0], "P1") ||
	    !field_is(f[1], l[1], "RESULT") || !uuid_is_valid(f[2], l[2]) ||
	    !field_is(f[5], l[5], "BRL") ||
	    !parse_canonical_u32(f[4], l[4], 1U, AMOUNT_MAX, &r.amount)) {
		return -1;
	}
	if (field_is(f[3], l[3], "PAID")) {
		r.status = PAYMENT_PAID;
	} else if (field_is(f[3], l[3], "CANCELLED")) {
		r.status = PAYMENT_CANCELLED;
	} else {
		return -1;
	}
	memcpy(r.uuid, f[2], UUID_LEN);
	r.uuid[UUID_LEN] = '\0';
	*out = r;
	return 0;
}
