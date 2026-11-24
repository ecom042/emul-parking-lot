/**
 * @file distance.h
 * @brief Dono único do ADC4: alterna as conversões assíncronas das duas faixas.
 *
 * Só há uma conversão pendente por vez. A conclusão é verificada sem bloquear
 * e precisa ser observada antes de DISTANCE_DEADLINE_MS desde a submissão.
 * Qualquer falha (submissão, resultado, amostra inválida ou prazo) trava o
 * ADC até o reset: não há nova submissão, nem para a outra faixa.
 */

#ifndef DISTANCE_H_
#define DISTANCE_H_

#include <stdbool.h>
#include <stdint.h>

#include "hw.h"

/** @brief Código abaixo do qual há veículo no ponto de identificação (~1000 mm). */
#define DISTANCE_ARRIVAL_CODE 683

/** @brief Código acima do qual o veículo saiu do ponto (~1500 mm). */
#define DISTANCE_CLEAR_CODE 1024

/** @brief Prazo para observar o fim de uma conversão, desde a submissão. */
#define DISTANCE_DEADLINE_MS 100

/** @brief Idade máxima de uma amostra para ela valer como atual. */
#define DISTANCE_FRESH_MS 20

/** @brief Maior código válido de 12 bits. */
#define DISTANCE_MAX_CODE 4095

/**
 * @brief Esquece amostras e falhas; a próxima conversão é da entrada.
 */
void distance_init(void);

/**
 * @brief Verifica a conversão pendente e, se ela terminou, inicia a da outra faixa.
 *
 * @param now_ms Tempo virtual atual.
 */
void distance_service(int64_t now_ms);

/**
 * @brief Devolve a última amostra de uma faixa, se ela for atual.
 *
 * @param lane Faixa.
 * @param now_ms Tempo virtual atual.
 * @param raw Recebe o código de 12 bits.
 * @return 0 se a amostra é atual, -ENODATA se não há amostra atual ou -EIO se o
 *         ADC está travado em falha.
 */
int distance_get(enum hw_lane lane, int64_t now_ms, uint16_t *raw);

/**
 * @brief Informa se o ADC está travado em falha até o reset.
 *
 * @return true se houve falha.
 */
bool distance_faulted(void);

#endif /* DISTANCE_H_ */
