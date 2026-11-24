/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file parking_limits.h
 * @brief Constantes e prazos do controlador do estacionamento.
 *
 * Uma constante existe uma vez so. A mudanca ao vivo da sabatina e a edicao
 * de uma linha aqui. Valores em milissegundos de tempo virtual.
 */

#ifndef STUDENT_PARKING_LIMITS_H_
#define STUDENT_PARKING_LIMITS_H_

/** Tick de servico da thread de controle: ADC e parsers. */
#define TICK_MS			10

/** Codigo ADC < este valor: carro chegou (cerca de 1000 mm). */
#define ADC_ARRIVE		683
/** Codigo ADC > este valor: carro saiu (cerca de 1500 mm). */
#define ADC_LEAVE		1024
/** Amostras consecutivas para aceitar uma transicao de presenca. */
#define ADC_DEBOUNCE_N		3
/** Validade de uma amostra de distancia. */
#define ADC_FRESH_MS		100
/** Prazo para observar a conclusao de uma conversao do ADC4. */
#define ADC_TIMEOUT_MS		100

/** Prazo de abertura e de fechamento da cancela. */
#define GATE_MOVE_MS		2500
/*
 * Sem constante para dois limites que o firmware cumpre pela arquitetura:
 * - a tolerancia de um tick (2510 ms) e do avaliador: o prazo acima e
 *   verificado a cada TICK_MS na thread de controle (lane.c, OPENING/CLOSING);
 * - o corte do motor na obstrucao (<= 10 ms) e imediato, na ISR do feixe
 *   (hw_io.c, beam_isr), sem espera configuravel.
 */
/** Prazo para concluir a passagem depois de abrir. */
#define PASS_MS			10000

/** Pulso do buzzer na assistencia. */
#define BUZZER_MS		100

/** Tentativas de captura da camera. */
#define CAPTURE_TRIES		3
/** Prazo de cada tentativa de captura. */
#define CAPTURE_MS		1000
/** Confianca minima aceita. */
#define CONF_MIN		85

/** Prazo para o display concluir cada tela da cobranca. */
#define DISPLAY_MS		1000

/** Envios totais de uma cobranca. */
#define CHARGE_TRIES		3
/** Prazo de cada envio. */
#define CHARGE_MS		1000

/** Quadro maximo de UART, incluindo o terminador. */
#define FRAME_MAX		128
/** Prazo de montagem de uma linha parcial. */
#define FRAME_MS		1000

/** Centavos da primeira unidade de tarifa. */
#define TARIFF_BASE		200
/** Centavos por unidade adicional. */
#define TARIFF_STEP		100
/** Acima de unidades, a faixa pede assistencia. */
#define TARIFF_UNITS_MAX	9999

/** Tamanho da placa: tres letras, um digito, um alfanumerico, dois digitos. */
#define PLATE_LEN		7

#endif /* STUDENT_PARKING_LIMITS_H_ */
