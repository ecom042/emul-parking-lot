/*
 * payment_parser.c — implementação do analisador P1.
 *
 * Mesma máquina de dois estados da câmera (MONTANDO / DESCARTANDO), mas com
 * terminador LF puro e uma gramática diferente:
 *
 *   linha   = "P1 " ( "RESULT " uuid " " status " " valor " BRL"
 *                   | "ERROR " código )
 *   status  = "PAID" | "CANCELLED"
 *   código  = FRAME | TIMEOUT | CAPACITY | CONFLICT | UNKNOWN
 */
#include <string.h>

#include "payment_parser.h"

#define LF '\n'

static bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static bool is_upper(char c)
{
	return c >= 'A' && c <= 'Z';
}

/* Só ASCII imprimível entra na linha. CR NÃO é permitido: o P1 termina em LF puro. */
static bool is_text_byte(uint8_t byte)
{
	return byte >= 32U && byte <= 126U;
}

bool payment_plate_valid(const char *plate)
{
	for (size_t i = 0U; i < 3U; ++i) {
		if (!is_upper(plate[i])) {
			return false;
		}
	}
	if (!is_digit(plate[3])) {
		return false;
	}
	if (!is_upper(plate[4]) && !is_digit(plate[4])) {
		return false;
	}
	return is_digit(plate[5]) && is_digit(plate[6]) && plate[7] == '\0';
}

/* Se *cursor começa com `literal`, avança sobre ele e devolve true. */
static bool consume(const char **cursor, const char *literal)
{
	size_t length = strlen(literal);

	if (strncmp(*cursor, literal, length) != 0) {
		return false;
	}
	*cursor += length;
	return true;
}

/* Número decimal canônico em [min, max] (sem sinal, sem zero à esquerda). */
static bool parse_number(const char **cursor, uint32_t min, uint32_t max, uint32_t *value)
{
	const char *p = *cursor;
	uint64_t number = 0U;
	size_t digits = 0U;

	while (is_digit(*p)) {
		if (digits >= 10U) {
			return false;
		}
		number = number * 10U + (uint64_t)(*p - '0');
		digits++;
		p++;
	}
	if (digits == 0U || (digits > 1U && **cursor == '0')) {
		return false;
	}
	if (number < min || number > max) {
		return false;
	}
	*value = (uint32_t)number;
	*cursor = p;
	return true;
}

/* Interpreta o que vem depois de "P1 RESULT ":  <uuid> <PAID|CANCELLED> <valor> BRL */
static bool parse_result(const char *p, struct payment_frame *frame)
{
	uint32_t amount;

	if (!payment_uuid_prefix_valid(p)) {
		return false;
	}
	memcpy(frame->uuid, p, PAYMENT_UUID_LEN);
	frame->uuid[PAYMENT_UUID_LEN] = '\0';
	p += PAYMENT_UUID_LEN;
	if (!consume(&p, " ")) {
		return false;
	}
	if (consume(&p, "PAID ")) {
		frame->status = PAYMENT_STATUS_PAID;
	} else if (consume(&p, "CANCELLED ")) {
		frame->status = PAYMENT_STATUS_CANCELLED;
	} else {
		return false;
	}
	if (!parse_number(&p, 1U, PAYMENT_AMOUNT_MAX, &amount)) {
		return false;
	}
	frame->amount = amount;
	return consume(&p, " BRL") && *p == '\0';
}

static bool error_code_valid(const char *code)
{
	static const char *const known[] = { "FRAME", "TIMEOUT", "CAPACITY", "CONFLICT", "UNKNOWN" };

	for (size_t i = 0U; i < sizeof(known) / sizeof(known[0]); ++i) {
		if (strcmp(code, known[i]) == 0) {
			return true;
		}
	}
	return false;
}

/* Uma linha chegou ao LF: confere a gramática inteira. */
static void classify(char *buf, size_t length, struct payment_frame *frame)
{
	const char *p = buf;

	buf[length] = '\0'; /* length <= 127: cabe no buffer de 128 */
	frame->kind = PAYMENT_FRAME_INVALID;
	if (!consume(&p, "P1 ")) {
		return;
	}
	if (consume(&p, "RESULT ")) {
		if (parse_result(p, frame)) {
			frame->kind = PAYMENT_FRAME_RESULT;
		}
	} else if (consume(&p, "ERROR ") && error_code_valid(p)) {
		frame->kind = PAYMENT_FRAME_ERROR;
	}
}

void payment_parser_init(struct payment_parser *parser)
{
	memset(parser, 0, sizeof(*parser));
}

void payment_parser_expire(struct payment_parser *parser, int64_t now)
{
	if (parser->len > 0U && now - parser->started_at >= PAYMENT_FRAME_TIMEOUT_MS) {
		parser->len = 0U;
		parser->discarding = true;
	}
}

enum payment_frame_kind payment_parser_feed(struct payment_parser *parser, uint8_t byte,
					    int64_t now, struct payment_frame *frame)
{
	frame->kind = PAYMENT_FRAME_NONE;
	payment_parser_expire(parser, now);

	if (byte == (uint8_t)LF) {
		bool was_discarding = parser->discarding;
		size_t length = parser->len;

		parser->len = 0U;
		parser->discarding = false;
		if (was_discarding) {
			frame->kind = PAYMENT_FRAME_INVALID;
		} else {
			classify(parser->buf, length, frame);
		}
		return frame->kind;
	}
	if (parser->discarding) {
		return frame->kind;
	}
	if (!is_text_byte(byte) || parser->len >= PAYMENT_FRAME_MAX - 1U) {
		parser->len = 0U;
		parser->discarding = true;
		return frame->kind;
	}
	if (parser->len == 0U) {
		parser->started_at = now;
	}
	parser->buf[parser->len++] = (char)byte;
	return frame->kind;
}
