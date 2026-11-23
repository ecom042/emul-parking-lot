/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file hw_adc.h
 * @brief ADC4 assincrono: dono unico, alternando os canais 17 e 18.
 *
 * Contrato congelado da dupla (fonte de verdade, secoes 2 e 9). Implementado
 * em hw_adc.c. Regras do contrato (docs/student-specification.md):
 * - uma conversao por vez, sem ressubmeter enquanto houver uma pendente;
 * - conclusao verificada sem bloquear e observada antes de ADC_TIMEOUT_MS;
 * - buffer, sequencia e sinal sobrevivem a uma conclusao atrasada;
 * - falha de submissao, de resultado, amostra invalida ou prazo vencido
 *   travam ate o reset.
 */

#ifndef STUDENT_HW_ADC_H_
#define STUDENT_HW_ADC_H_

#include <stdint.h>

struct lane_cfg;

/**
 * @brief Prepara o ADC4 e os dois canais e inicia a aquisicao.
 * @retval 0 Sucesso.
 * @retval <0 errno negativo.
 */
int hw_adc_start(void);

/**
 * @brief Entrega a amostra mais recente do canal da faixa.
 *
 * Transicoes que dependem de distancia so usam amostras com menos de
 * ADC_FRESH_MS.
 *
 * @param cfg Faixa.
 * @param code Recebe o codigo de 12 bits (0..4095).
 * @param ts_ms Recebe o k_uptime_get() em que a amostra foi lida.
 * @retval 0 Sucesso.
 * @retval <0 errno negativo.
 */
int hw_adc_read(const struct lane_cfg *cfg, uint16_t *code, int64_t *ts_ms);

#endif /* STUDENT_HW_ADC_H_ */
