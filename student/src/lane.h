/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file lane.h
 * @brief Maquina de estados de uma faixa. As duas faixas usam este codigo;
 *        muda so a configuracao (struct lane_cfg).
 *
 * Os estados e as transicoes estao na secao "Desenho da dupla" do guia, ja
 * corrigidos pelo docs/ do curso. Decisao do aluno B: sem WAIT_BEAM_CLEAR,
 * porque a passagem so completa com o feixe ja livre.
 */

#ifndef STUDENT_LANE_H_
#define STUDENT_LANE_H_

#include <stdbool.h>
#include <stdint.h>

#include "events.h"
#include "lane_cfg.h"
#include "parking_limits.h"
#include "presence.h"
#include "uuid.h"

/** Estados de uma faixa. */
enum lane_state {
	LANE_IDLE,            /**< Cancela fechada, esperando carro. */
	LANE_IDENTIFYING,     /**< Ate CAPTURE_TRIES capturas. */
	LANE_CHARGING,        /**< So na saida: cobranca em andamento. */
	LANE_OPENING,         /**< Abrindo, prazo GATE_MOVE_MS. */
	LANE_WAIT_PASSAGE,    /**< Aberta, prazo PASS_MS; a passagem completa fecha. */
	LANE_CLOSING,         /**< Fechando, prazo GATE_MOVE_MS. */
	LANE_ASSIST,          /**< Assistencia rearmavel. */
	LANE_LOCKED,          /**< Assistencia travada ate o reset. */
};

/** Estado de uma faixa. */
struct lane {
	const struct lane_cfg *cfg; /**< Configuracao recebida em lane_init(). */
	enum lane_state state;      /**< Estado atual. */
	int64_t deadline_ms;        /**< Prazo do estado atual. */

	/* Ultimas entradas vistas. */
	bool started;               /**< Partida ja classificada (1o EV_LIMITS). */
	bool upper;                 /**< Fim de curso superior acionado. */
	bool lower;                 /**< Fim de curso inferior acionado. */
	bool beam;                  /**< Feixe ativo. */
	uint16_t code;              /**< Ultimo codigo do ADC4. */
	int64_t code_ms;            /**< Quando @c code chegou. */
	bool have_code;             /**< Ja chegou alguma amostra. */
	struct presence presence;   /**< Histerese e debounce da chegada. */
	bool served;                /**< A presenca atual ja foi identificada. */

	/* Ciclo de autorizacao em andamento. */
	uint32_t id_counter;        /**< Ultimo ID de captura usado. */
	uint8_t tries;              /**< Capturas ou envios da cobranca feitos. */
	uint32_t capture_id;        /**< ID esperado no +PLATE; 0 = nenhum. */
	char plate[PLATE_LEN + 1];  /**< Placa aceita neste ciclo. */
	bool beam_seen;             /**< Na passagem: feixe ja ficou ativo. */
	bool beam_cleared;          /**< Na passagem: e depois inativo. */

	/* Cobranca (so na saida). */
	char uuid[UUID_LEN + 1];    /**< UUID da cobranca ativa; vazio = nenhuma. */
	uint32_t cents;             /**< Valor congelado da cobranca. */
	char cancel_uuid[UUID_LEN + 1]; /**< UUID ja invalidado a cancelar. */
	bool cancel_pending;        /**< P1 CANCEL ainda nao saiu pela UART. */
};

/**
 * @brief Prepara uma faixa para a partida segura.
 *
 * Motor desligado, vermelho aceso, verde apagado. A posicao e classificada
 * no primeiro EV_LIMITS: so inferior -> IDLE; os dois -> LOCKED; outro caso
 * -> ASSIST (sem fechar sozinha).
 */
void lane_init(struct lane *lane, const struct lane_cfg *cfg);

/** @brief Entrega um evento a maquina de estados da faixa. */
void lane_handle_event(struct lane *lane, const struct lane_event *ev);

#endif /* STUDENT_LANE_H_ */
