/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file at_parser.h
 * @brief Analisador da camera: protocolo AT da disciplina, linhas em CR LF.
 *
 * Um byte por vez, sem alocacao e com buffer fixo de FRAME_MAX bytes,
 * incluindo o terminador. Linha malformada ou longa demais e descartada ate
 * o LF seguinte, sem executar prefixo valido.
 */

#ifndef STUDENT_AT_PARSER_H_
#define STUDENT_AT_PARSER_H_

#include <stdbool.h>
#include <stdint.h>

#include "parking_limits.h"

/** Resultado de uma captura aceita. */
struct at_plate {
	uint32_t id;
	char plate[PLATE_LEN + 1];
	uint8_t confidence;
};

/** Estado de montagem de uma linha da camera. */
struct at_parser {
	char line[FRAME_MAX];
	uint16_t len;
	bool discarding;
	bool have_first;
	int64_t first_ms;
};

/** Zera o analisador, como no boot ou depois de um reset. */
void at_parser_reset(struct at_parser *p);

/**
 * @brief Consome um byte da camera.
 *
 * @param p Analisador da faixa.
 * @param c Byte recebido.
 * @param now_ms Tempo virtual da entrega do byte.
 * @param out Recebe a captura quando um +PLATE valido fecha a linha.
 * @retval 1 Fechou um +PLATE valido.
 * @retval 0 Byte consumido sem quadro, ou quadro que nao e placa (OK, ERROR).
 * @retval -EINVAL Argumento nulo.
 */
int at_parser_feed(struct at_parser *p, char c, int64_t now_ms,
		   struct at_plate *out);

#endif /* STUDENT_AT_PARSER_H_ */
