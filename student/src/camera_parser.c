/*
 * camera_parser.c — implementação do analisador de linhas da câmera.
 * Veja camera_parser.h para a visão geral. Aqui estão os detalhes.
 *
 * MÁQUINA DE ESTADOS (só dois estados, guardados em `parser`):
 *
 *      ┌──────────── byte comum ────────────┐
 *      ▼                                    │
 *  [MONTANDO]  (discarding == false) ───────┘
 *      │  │
 *      │  └─ LF ──────────► classifica a linha e entrega o quadro
 *      │
 *      └─ byte proibido / linha longa demais / prazo vencido
 *                 ▼
 *  [DESCARTANDO] (discarding == true): ignora tudo até o LF
 *      └─ LF ──────────► devolve INVALID e volta para MONTANDO
 *
 * Por que "descartar até o LF" em vez de tentar aproveitar o resto?
 *   Porque o LF é a única marca de fronteira entre quadros. Depois dele,
 *   sabemos que o próximo byte é o INÍCIO de uma linha nova (ressincroniza).
 *   Se tentássemos aproveitar um pedaço no meio, poderíamos "executar" um
 *   prefixo que parece válido — o contrato proíbe isso.
 */
#include <string.h>

#include "camera_parser.h"

#define CR '\r'
#define LF '\n'
#define PLATE_PREFIX "+PLATE:"

/* ---------------------------------------------------------------------------
 * Funções auxiliares de classificação de caracteres (sem <ctype.h>, de
 * propósito: queremos ASCII estrito, sem depender de "locale").
 * ------------------------------------------------------------------------- */
static bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static bool is_upper(char c)
{
	return c >= 'A' && c <= 'Z';
}

/* Um byte pode entrar na linha se for ASCII imprimível (32..126) ou o CR. */
static bool is_text_byte(uint8_t byte)
{
	return (byte >= 32U && byte <= 126U) || byte == (uint8_t)CR;
}

/*
 * Placa válida: [A-Z]{3}[0-9][A-Z0-9][0-9]{2}  (ex.: ABC1D23 ou ABC1234).
 * Cada `if` devolve false no PRIMEIRO caractere errado. Isso também protege
 * contra ler além do '\0' final: o '\0' não é letra nem dígito, então a
 * verificação para nele.
 */
static bool plate_valid(const char *plate)
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
	return is_digit(plate[5]) && is_digit(plate[6]);
}

/*
 * Lê um número decimal CANÔNICO a partir de *cursor e avança o cursor.
 * "Canônico" = só dígitos, sem sinal, sem espaço e sem zero à esquerda
 * ("0" sozinho pode; "01" não). Também exige min <= valor <= max.
 * O limite de 10 dígitos evita estouro na conta e rejeita lixo enorme.
 */
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

/*
 * Interpreta o que vem DEPOIS de "+PLATE:" :   17,"ABC1D23",93
 * Cada passo confere um pedaço e, se algo estiver errado, desiste (false).
 * Só quando a linha INTEIRA bate é que o quadro é considerado PLATE.
 */
static bool parse_plate_fields(const char *text, struct camera_frame *frame)
{
	const char *p = text;
	uint32_t id;
	uint32_t confidence;

	if (!parse_number(&p, 1U, CAMERA_ID_MAX, &id)) {
		return false; /* id inválido: 0, com zero à esquerda, grande demais... */
	}
	if (p[0] != ',' || p[1] != '"') {
		return false; /* esperado: vírgula e aspas abrindo a placa */
	}
	p += 2;
	if (!plate_valid(p) || p[7] != '"' || p[8] != ',') {
		return false; /* placa inválida, ou faltou aspas/vírgula depois dela */
	}
	memcpy(frame->plate, p, CAMERA_PLATE_LEN);
	frame->plate[CAMERA_PLATE_LEN] = '\0';
	p += 9;
	if (!parse_number(&p, 0U, CAMERA_CONFIDENCE_MAX, &confidence) || *p != '\0') {
		return false; /* confiança fora de 0..100, ou sobrou lixo no fim */
	}
	frame->id = id;
	frame->confidence = (uint8_t)confidence;
	return true;
}

/*
 * Uma linha chegou ao LF. `buf` tem `length` bytes (sem o LF). Para ser
 * válida ela precisa terminar em CR, não ter outro CR no meio e ser
 * exatamente OK, ERROR ou +PLATE:... Qualquer outra coisa é INVALID.
 */
static void classify(char *buf, size_t length, struct camera_frame *frame)
{
	frame->kind = CAMERA_FRAME_INVALID;
	if (length == 0U || buf[length - 1U] != CR) {
		return; /* LF sem CR antes: a câmera sempre termina com CR LF */
	}
	buf[length - 1U] = '\0'; /* troca o CR por '\0': agora buf é uma string C */
	if (memchr(buf, CR, length - 1U) != NULL) {
		return; /* CR no meio do corpo não é permitido */
	}
	if (strcmp(buf, "OK") == 0) {
		frame->kind = CAMERA_FRAME_OK;
	} else if (strcmp(buf, "ERROR") == 0) {
		frame->kind = CAMERA_FRAME_ERROR;
	} else if (strncmp(buf, PLATE_PREFIX, sizeof(PLATE_PREFIX) - 1U) == 0 &&
		   parse_plate_fields(buf + sizeof(PLATE_PREFIX) - 1U, frame)) {
		frame->kind = CAMERA_FRAME_PLATE;
	}
}

void camera_parser_init(struct camera_parser *parser)
{
	memset(parser, 0, sizeof(*parser));
}

void camera_parser_expire(struct camera_parser *parser, int64_t now)
{
	/*
	 * Só existe "linha parcial" quando len > 0 (e, nesse caso, não estamos
	 * descartando: ao descartar, len é zerado). Se passaram 1000 ms desde o
	 * primeiro byte, desistimos da linha e passamos a descartar até o LF.
	 */
	if (parser->len > 0U && now - parser->started_at >= CAMERA_FRAME_TIMEOUT_MS) {
		parser->len = 0U;
		parser->discarding = true;
	}
}

enum camera_frame_kind camera_parser_feed(struct camera_parser *parser, uint8_t byte,
					  int64_t now, struct camera_frame *frame)
{
	frame->kind = CAMERA_FRAME_NONE;

	/* 1) Antes de qualquer coisa, vale o prazo da linha parcial. */
	camera_parser_expire(parser, now);

	/* 2) LF: fim de linha. Ou entregamos o quadro, ou jogamos o lixo fora. */
	if (byte == (uint8_t)LF) {
		bool was_discarding = parser->discarding;
		size_t length = parser->len;

		parser->len = 0U;
		parser->discarding = false;
		if (was_discarding) {
			frame->kind = CAMERA_FRAME_INVALID;
		} else {
			classify(parser->buf, length, frame);
		}
		return frame->kind;
	}

	/* 3) Estamos descartando até o LF: ignora o byte. */
	if (parser->discarding) {
		return frame->kind;
	}

	/*
	 * 4) Byte proibido, ou linha já com 127 bytes (o 128º tem de ser o LF):
	 *    a linha é inválida. Começa a descartar até o LF.
	 */
	if (!is_text_byte(byte) || parser->len >= CAMERA_FRAME_MAX - 1U) {
		parser->len = 0U;
		parser->discarding = true;
		return frame->kind;
	}

	/* 5) Byte normal: guarda. O prazo de 1000 ms conta do PRIMEIRO byte. */
	if (parser->len == 0U) {
		parser->started_at = now;
	}
	parser->buf[parser->len++] = (char)byte;
	return frame->kind;
}
