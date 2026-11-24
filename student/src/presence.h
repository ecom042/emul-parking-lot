/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file presence.h
 * @brief Presenca do carro: histerese ADC_ARRIVE/ADC_LEAVE e debounce de
 *        ADC_DEBOUNCE_N amostras consecutivas.
 *
 * Contrato congelado da dupla (fonte de verdade, secoes 2, 4 e 9).
 * Implementado em presence.c. Um evento so na transicao: carro parado nao
 * gera segunda chegada, e ruido de +-64 codigos nao gera chegada falsa.
 * Qualquer amostra fora da faixa zera o contador de consecutivas.
 */

#ifndef STUDENT_PRESENCE_H_
#define STUDENT_PRESENCE_H_

#include <stdbool.h>
#include <stdint.h>

/** Resultado de uma amostra. */
enum presence_change {
	PRESENCE_NONE,    /**< Nada mudou. */
	PRESENCE_ARRIVED, /**< Carro chegou: vira EV_CAR_ARRIVED. */
	PRESENCE_LEFT,    /**< Carro saiu: vira EV_CAR_LEFT. */
};

/** Estado de presenca de uma faixa. */
struct presence {
	bool present;  /**< Ultima transicao aceita foi uma chegada. */
	uint8_t count; /**< Amostras consecutivas na direcao oposta. */
};

/**
 * @brief Processa uma amostra recente do ADC4.
 * @param p Estado da faixa.
 * @param code Codigo de 12 bits.
 * @return A transicao aceita, ou PRESENCE_NONE.
 */
enum presence_change presence_update(struct presence *p, uint16_t code);

#endif /* STUDENT_PRESENCE_H_ */
