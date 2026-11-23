/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file events.h
 * @brief Eventos que alimentam a maquina de estados de uma faixa.
 *
 * Quem produz: controller.c, a cada tick da thread de controle, a partir da
 * camada de hardware (hw.h, hw_adc.h, hw_uart.h). Quem consome:
 * lane_handle_event(). Todo evento carrega o tempo virtual em que foi
 * produzido, para a logica nao depender do relogio (e poder ser testada).
 */

#ifndef STUDENT_EVENTS_H_
#define STUDENT_EVENTS_H_

#include <stdbool.h>
#include <stdint.h>

#include "at_parser.h"
#include "p1_parser.h"

/** Identificador de um evento de faixa. */
enum lane_ev_id {
	EV_TICK,      /**< Servico periodico, a cada TICK_MS: prazos e guardas. */
	EV_LIMITS,    /**< Niveis dos fins de curso; dados em @c limits. */
	EV_DISTANCE,  /**< Amostra nova do ADC4; codigo em @c code. */
	EV_BEAM_ON,   /**< Feixe ficou ativo. */
	EV_BEAM_OFF,  /**< Feixe ficou inativo. */
	EV_PLATE,     /**< Quadro +PLATE valido; dados em @c plate. */
	EV_PAYMENT,   /**< P1 RESULT ou P1 ERROR; dados em @c payment. */
	EV_ADC_FAULT, /**< Falha do ADC4: travada ate o reset. */
	EV_HW_ERROR,  /**< Erro de driver ao ler ou comandar a faixa. */
};

/** Niveis dos dois fins de curso. */
struct lane_limits {
	bool upper; /**< Fim de curso superior acionado (aberta). */
	bool lower; /**< Fim de curso inferior acionado (fechada). */
};

/** Evento entregue a lane_handle_event(). */
struct lane_event {
	enum lane_ev_id id;
	int64_t now_ms; /**< Tempo virtual (k_uptime_get()) do evento. */
	union {
		struct lane_limits limits; /**< Valido em EV_LIMITS. */
		uint16_t code;             /**< Valido em EV_DISTANCE. */
		struct at_plate plate;     /**< Valido em EV_PLATE. */
		struct p1_result payment;  /**< Valido em EV_PAYMENT. */
	};
};

#endif /* STUDENT_EVENTS_H_ */
