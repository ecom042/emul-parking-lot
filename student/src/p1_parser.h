/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file p1_parser.h
 * @brief Analisador do terminal de pagamento: P1, linhas so em LF.
 *
 * Mesmo esqueleto do parser da camera (buffer fixo, prazo de montagem,
 * descarte ate o LF), com duas diferencas do contrato: o terminador e so LF e
 * um CR na linha e erro.
 */

#ifndef STUDENT_P1_PARSER_H_
#define STUDENT_P1_PARSER_H_

#include <stdbool.h>
#include <stdint.h>

#include "parking_limits.h"
#include "uuid.h"

/** Resultado de uma cobranca. */
enum p1_status {
	P1_STATUS_PAID,
	P1_STATUS_CANCELLED,
	P1_STATUS_ERROR,
};

/** Codigos de P1 ERROR. Sao diagnostico, nao autorizacao. */
enum p1_error {
	P1_ERROR_NONE,
	P1_ERROR_FRAME,
	P1_ERROR_TIMEOUT,
	P1_ERROR_CAPACITY,
	P1_ERROR_CONFLICT,
	P1_ERROR_UNKNOWN,
};

/** Quadro aceito do terminal. */
struct p1_result {
	enum p1_status status;
	enum p1_error error;
	char uuid[UUID_LEN + 1];
	uint32_t cents;
};

/** Estado de montagem de uma linha do terminal. */
struct p1_parser {
	char line[FRAME_MAX];
	uint16_t len;
	bool discarding;
	bool have_first;
	int64_t first_ms;
};

/** Zera o analisador, como no boot ou depois de um reset. */
void p1_parser_reset(struct p1_parser *p);

/**
 * @brief Consome um byte do terminal.
 *
 * @param p Analisador do terminal.
 * @param c Byte recebido.
 * @param now_ms Tempo virtual da entrega do byte.
 * @param out Recebe o quadro quando uma linha valida fecha.
 * @retval 1 Fechou um P1 RESULT ou P1 ERROR valido.
 * @retval 0 Byte consumido sem quadro, ou linha descartada.
 * @retval -EINVAL Argumento nulo.
 */
int p1_parser_feed(struct p1_parser *p, char c, int64_t now_ms,
		   struct p1_result *out);

#endif /* STUDENT_P1_PARSER_H_ */
