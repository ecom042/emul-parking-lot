/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file events.h
 * @brief Eventos que alimentam a maquina de estados de uma faixa.
 *
 * Contrato congelado da dupla (fonte de verdade, secao 2). Diferenca: nao ha
 * EV_DISPLAY_DONE, porque o display (st7789.h) nao avisa a conclusao.
 */

#ifndef STUDENT_EVENTS_H_
#define STUDENT_EVENTS_H_

#include "at_parser.h"
#include "p1_parser.h"

/** Identificador de um evento de faixa. */
enum lane_ev_id {
	EV_TICK,            /**< Servico periodico, a cada TICK_MS. */
	EV_DEADLINE,        /**< O prazo do estado atual venceu. */
	EV_CAR_ARRIVED,     /**< Presenca: codigo < ADC_ARRIVE, com debounce. */
	EV_CAR_LEFT,        /**< Presenca: codigo > ADC_LEAVE, com debounce. */
	EV_LIMIT_UPPER,     /**< Fim de curso superior acionado. */
	EV_LIMIT_LOWER,     /**< Fim de curso inferior acionado. */
	EV_LIMITS_CONFLICT, /**< Superior e inferior ao mesmo tempo. */
	EV_BEAM_ON,         /**< Feixe ficou ativo. */
	EV_BEAM_OFF,        /**< Feixe ficou inativo. */
	EV_PLATE,           /**< Quadro +PLATE valido; dados em @c plate. */
	EV_PAYMENT,         /**< P1 RESULT ou P1 ERROR; dados em @c payment. */
	EV_ADC_FAULT,       /**< Falha do ADC4: travada ate o reset. */
	EV_HW_ERROR,        /**< Erro de driver ao comandar a faixa. */
};

/** Evento entregue a lane_handle_event(). */
struct lane_event {
	enum lane_ev_id id;
	union {
		struct at_plate plate;    /**< Valido em EV_PLATE. */
		struct p1_result payment; /**< Valido em EV_PAYMENT. */
	};
};

#endif /* STUDENT_EVENTS_H_ */
