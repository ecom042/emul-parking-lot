/**
 * @file lane.h
 * @brief Máquina de estados de uma faixa: identificação, pagamento (só na
 *        saída), abertura, passagem, fechamento e assistência.
 *
 * As duas faixas usam o mesmo código e só dividem o ADC4 e a tabela de sessões.
 * lane_step() não bloqueia: é chamada pelo laço principal e a cada borda de
 * entrada, sempre com o mesmo lock, para cada faixa.
 */

#ifndef LANE_H_
#define LANE_H_

#include <stdint.h>

#include "hw.h"

/** @brief Estados de uma faixa. */
enum lane_state {
	LANE_BOOT = 0,    /**< Esperando a primeira amostra dos sensores. */
	LANE_IDLE,        /**< Fechada, esperando veículo. */
	LANE_IDENTIFYING, /**< Tentativas de captura da placa. */
	LANE_SHOWING_PLATE, /**< Saída: esperando o desenho da placa. */
	LANE_SHOWING_CHARGE, /**< Saída: esperando o valor e os avisos no painel. */
	LANE_PAYING,      /**< Saída: aguardando o PAID da cobrança. */
	LANE_OPENING,     /**< Motor abrindo. */
	LANE_OPEN,        /**< Aberta, esperando o feixe ser interrompido. */
	LANE_PASSING,     /**< Feixe visto, esperando ele liberar com o carro longe. */
	LANE_CLOSING,     /**< Motor fechando. */
	LANE_ASSIST,      /**< Assistência com cancela fechada; rearma quando o carro sai. */
	LANE_FAULT,       /**< Falha travada até o reset. */
};

/** @brief Tempo para os sensores do modelo publicarem a primeira amostra. */
#define LANE_SETTLE_MS 20

/** @brief Orçamento de cada movimento do motor. */
#define LANE_MOTOR_BUDGET_MS 2500

/** @brief Orçamento da passagem, contado da abertura completa. */
#define LANE_PASSAGE_BUDGET_MS 10000

/** @brief Prazo de cada tentativa de captura. */
#define LANE_CAPTURE_TIMEOUT_MS 1000

/** @brief Tentativas de captura antes da assistência. */
#define LANE_CAPTURE_ATTEMPTS 3

/** @brief Duração do pulso do buzzer na assistência. */
#define LANE_BUZZER_PULSE_MS 100

/**
 * @brief Coloca a faixa no estado inicial (saídas já seguras por hw_init()).
 *
 * @param lane Faixa.
 * @param now_ms Tempo virtual atual.
 */
void lane_init(enum hw_lane lane, int64_t now_ms);

/**
 * @brief Trava a faixa em falha, com assistência.
 *
 * @param lane Faixa.
 * @param now_ms Tempo virtual atual.
 */
void lane_fail(enum hw_lane lane, int64_t now_ms);

/**
 * @brief Atende a faixa: sensores, prazos, câmera, pagamento e buzzer.
 *
 * @param lane Faixa.
 * @param now_ms Tempo virtual atual.
 */
void lane_step(enum hw_lane lane, int64_t now_ms);

/**
 * @brief Informa o estado atual de uma faixa.
 *
 * @param lane Faixa.
 * @return Estado.
 */
enum lane_state lane_state_get(enum hw_lane lane);

#endif /* LANE_H_ */
