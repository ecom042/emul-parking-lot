/*
 * Protocolo P1 do terminal de pagamento (REQ-05, REQ-11): só sintaxe e UUID.
 *
 *   firmware → terminal  P1 CHARGE <uuid> <placa> <centavos> BRL\n
 *                        P1 CANCEL <uuid>\n
 *   terminal → firmware  P1 RESULT <uuid> PAID <centavos> BRL
 *                        P1 RESULT <uuid> CANCELLED <centavos> BRL
 *                        P1 ERROR <FRAME|TIMEOUT|CAPACITY|CONFLICT|UNKNOWN>
 *
 * As linhas chegam já montadas pelo line_parser (sem o LF).
 */
#ifndef PARKING_PAYMENT_H
#define PARKING_PAYMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* UUID: 8-4-4-4-12 dígitos hexadecimais minúsculos. */
#define UUID_LEN 36
#define UUID_BUF (UUID_LEN + 1)

/* Faixa canônica do valor em centavos. */
#define PAYMENT_AMOUNT_MIN 1U
#define PAYMENT_AMOUNT_MAX 1000000U

enum payment_kind {
	PAYMENT_INVALID = 0, /* malformada: descartar */
	PAYMENT_PAID,
	PAYMENT_CANCELLED,
	PAYMENT_ERROR,       /* sem correlação: só diagnóstico */
};

struct payment_msg {
	enum payment_kind kind;
	char uuid[UUID_BUF]; /* PAID/CANCELLED */
	uint32_t amount;     /* PAID/CANCELLED */
};

/** "P1 CHARGE ...\n". @return tamanho ou -EINVAL. */
int payment_format_charge(char *out, size_t size, const char *uuid, const char *plate,
			  uint32_t amount);

/** "P1 CANCEL <uuid>\n". @return tamanho ou -EINVAL. */
int payment_format_cancel(char *out, size_t size, const char *uuid);

/** Interpreta uma linha do terminal. Sempre preenche m->kind. */
void payment_parse(const char *line, struct payment_msg *m);

/** true se for um UUID no formato do P1. */
bool payment_uuid_valid(const char *uuid);

/**
 * Gera um UUID novo (formato v4) a partir do gerador interno, misturando
 * `entropy` (o contador de ciclos no momento do pedido) e um contador.
 */
void payment_new_uuid(char out[UUID_BUF], uint32_t entropy);

#endif /* PARKING_PAYMENT_H */
