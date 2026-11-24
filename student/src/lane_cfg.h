/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file lane_cfg.h
 * @brief Configuracao logica de cada faixa.
 *
 * So dados logicos: os pinos e o canal do ADC4 de cada faixa ficam escondidos
 * em hw_io.c e hw_adc.c, indexados por @c id. Assim lane.c nunca inclui um
 * header de driver e pode ser testado com FFF.
 */

#ifndef STUDENT_LANE_CFG_H_
#define STUDENT_LANE_CFG_H_

#include <stdbool.h>

#include "hw_uart.h"

/** Faixas do estacionamento; tambem e o indice das tabelas de hardware. */
enum lane_id {
	LANE_ENTRY,
	LANE_EXIT,
	LANE_COUNT,
};

/** Configuracao de uma faixa. As duas instancias ficam em lane_cfg.c. */
struct lane_cfg {
	enum lane_id id;          /**< Indice nas tabelas de hw_io.c e hw_adc.c. */
	enum hw_uart_port camera; /**< UART da camera desta faixa. */
	bool has_payment;         /**< So a saida cobra, com terminal e display. */
};

/** As duas faixas, na ordem de enum lane_id. */
extern const struct lane_cfg lane_cfgs[LANE_COUNT];

#endif /* STUDENT_LANE_CFG_H_ */
