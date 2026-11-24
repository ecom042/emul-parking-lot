/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file hw_adc.h
 * @brief ADC4 assincrono: dono unico, alternando os canais 17 e 18.
 *
 * Implementado em hw_adc.c. Regras do contrato
 * (docs/student-specification.md e docs/device-protocols.md):
 * - uma conversao por vez, sem ressubmeter enquanto houver uma pendente;
 * - conclusao verificada sem bloquear e observada antes de ADC_TIMEOUT_MS;
 * - buffer, sequencia e sinal sao estaticos: sobrevivem a uma conclusao
 *   atrasada;
 * - falha de submissao, de resultado, amostra invalida ou prazo vencido
 *   travam ate o reset; nenhuma conclusao atrasada rearma a aquisicao.
 */

#ifndef STUDENT_HW_ADC_H_
#define STUDENT_HW_ADC_H_

#include <stdint.h>

#include "lane_cfg.h"

/** Amostra de distancia de uma faixa. */
struct hw_adc_sample {
	enum lane_id lane; /**< Faixa dona do canal convertido. */
	uint16_t code;     /**< Codigo de 12 bits, 0..4095. */
};

/**
 * @brief Prepara o ADC4 e os dois canais. Nao converte nada ainda.
 * @retval 0 Sucesso.
 * @retval <0 errno negativo; trata-se como falha do ADC.
 */
int hw_adc_init(void);

/**
 * @brief Servico nao bloqueante, chamado a cada tick pela thread de controle.
 *
 * Verifica a conversao pendente; quando terminou dentro do prazo, entrega a
 * amostra e submete a do outro canal. Sem conversao pendente, submete uma.
 *
 * @param now_ms Tempo virtual atual (k_uptime_get()).
 * @param out Recebe a amostra quando o retorno e 1.
 * @retval 1 Uma amostra nova em @p out.
 * @retval 0 Nada novo (conversao ainda dentro do prazo).
 * @retval <0 Falha do ADC, travada ate o reset.
 */
int hw_adc_poll(int64_t now_ms, struct hw_adc_sample *out);

#endif /* STUDENT_HW_ADC_H_ */
