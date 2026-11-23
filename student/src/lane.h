/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file lane.h
 * @brief Maquina de estados de uma faixa. As duas faixas usam este codigo;
 *        muda so a configuracao (struct lane_cfg).
 *
 * Contrato congelado da dupla (fonte de verdade, secao 2). Implementado em
 * lane.c. Os estados e as transicoes estao na secao "Desenho da dupla" do
 * guia, ja corrigidos pelo docs/ do curso.
 */

#ifndef STUDENT_LANE_H_
#define STUDENT_LANE_H_

#include "events.h"
#include "hw.h"

/** Estados de uma faixa. */
enum lane_state {
	LANE_IDLE,            /**< Cancela fechada, esperando carro. */
	LANE_IDENTIFYING,     /**< Ate CAPTURE_TRIES capturas. */
	LANE_CHARGING,        /**< So na saida: cobranca em andamento. */
	LANE_OPENING,         /**< Abrindo, prazo GATE_MOVE_MS. */
	LANE_WAIT_PASSAGE,    /**< Aberta, prazo PASS_MS. */
	LANE_WAIT_BEAM_CLEAR, /**< Passagem completa; espera feixe livre. */
	LANE_CLOSING,         /**< Fechando, prazo GATE_MOVE_MS. */
	LANE_ASSIST,          /**< Assistencia rearmavel. */
	LANE_LOCKED,          /**< Assistencia travada ate o reset. */
};

/** Estado de uma faixa. */
struct lane {
	const struct lane_cfg *cfg; /**< Configuracao recebida em lane_init(). */
	enum lane_state state;      /**< Estado atual. */
	/* Os demais campos (tentativa, ID, UUID, preco, prazos) sao do aluno B. */
};

/** @brief Prepara uma faixa para a partida segura. */
void lane_init(struct lane *lane, const struct lane_cfg *cfg);

/** @brief Entrega um evento a maquina de estados da faixa. */
void lane_handle_event(struct lane *lane, const struct lane_event *ev);

#endif /* STUDENT_LANE_H_ */
