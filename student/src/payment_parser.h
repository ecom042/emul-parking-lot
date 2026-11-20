/*
 * payment_parser.h — Analisador das linhas do terminal de pagamento (protocolo P1)
 * ================================================================================
 *
 * PARA QUE SERVE?
 *   O terminal responde por UART com linhas de TEXTO terminadas só em LF:
 *
 *       P1 RESULT 3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1 PAID 400 BRL\n
 *       P1 RESULT <uuid> CANCELLED 400 BRL\n
 *       P1 ERROR FRAME\n            (diagnóstico; nunca decide nada)
 *
 *   Como na câmera, os bytes chegam fragmentados. Este módulo monta a linha e a
 *   valida RIGIDAMENTE. É o "segundo analisador independente" do REQ-11: tem
 *   buffer, prazo e estado próprios, separados do analisador da câmera.
 *
 * O QUE GARANTE (REQ-11, REQ-05):
 *   - quadro de no máximo 128 bytes, contando o LF;
 *   - linha parcial vence 1000 ms depois do primeiro byte;
 *   - linha inválida (byte fora de ASCII 32..126 — inclusive CR —, longa demais,
 *     campo malformado) é descartada ATÉ O PRÓXIMO LF; nada de aproveitar prefixo;
 *   - campos separados por UM espaço; UUID 8-4-4-4-12 minúsculo; valor em
 *     centavos canônico (1..1000000, sem zero à esquerda); moeda "BRL".
 *   Uma resposta como "P1 RESULT <uuid> PAID broken BRL" (valor quebrado) vira
 *   INVALID e jamais pode abrir a cancela.
 *
 * O QUE NÃO FAZ: decidir se o PAID é o "atual" e tem o mesmo valor. Isso é do
 * payment.c. Aqui só se diz "chegou um RESULT com estes campos".
 */
#ifndef PARKING_PAYMENT_PARSER_H
#define PARKING_PAYMENT_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "payment_uuid.h"

#define PAYMENT_FRAME_MAX 128U            /* inclui o LF */
#define PAYMENT_FRAME_TIMEOUT_MS 1000     /* prazo de uma linha parcial */
#define PAYMENT_AMOUNT_MAX 1000000U       /* limite do terminal, em centavos */

enum payment_frame_kind {
	PAYMENT_FRAME_NONE,    /* ainda não há quadro completo */
	PAYMENT_FRAME_RESULT,  /* P1 RESULT ... (PAID ou CANCELLED) */
	PAYMENT_FRAME_ERROR,   /* P1 ERROR <código>: só diagnóstico */
	PAYMENT_FRAME_INVALID, /* linha terminou mas estava malformada: descartada */
};

enum payment_status {
	PAYMENT_STATUS_PAID,
	PAYMENT_STATUS_CANCELLED,
};

/* Campos de um RESULT (só valem quando kind == PAYMENT_FRAME_RESULT). */
struct payment_frame {
	enum payment_frame_kind kind;
	char uuid[PAYMENT_UUID_SIZE];
	enum payment_status status;
	uint32_t amount; /* centavos */
};

/* Estado de UM analisador. */
struct payment_parser {
	char buf[PAYMENT_FRAME_MAX]; /* até 127 bytes de linha em montagem */
	size_t len;
	bool discarding;    /* true: jogando fora até o próximo LF */
	int64_t started_at; /* instante do 1º byte da linha em montagem */
};

void payment_parser_init(struct payment_parser *parser);

/* Entrega um byte no instante `now` (ms virtuais); devolve o quadro que ele concluiu (ou NONE). */
enum payment_frame_kind payment_parser_feed(struct payment_parser *parser, uint8_t byte,
					    int64_t now, struct payment_frame *frame);

/* Aplica o prazo de 1000 ms a uma linha parcial (feed já faz isso sozinho). */
void payment_parser_expire(struct payment_parser *parser, int64_t now);

/* Placa válida e terminada em '\0' na 8ª posição: [A-Z]{3}[0-9][A-Z0-9][0-9]{2}. */
bool payment_plate_valid(const char *plate);

#endif /* PARKING_PAYMENT_PARSER_H */
