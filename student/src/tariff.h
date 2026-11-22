/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file tariff.h
 * @brief Tarifa inteira, em centavos, sem ponto flutuante.
 */

#ifndef STUDENT_TARIFF_H_
#define STUDENT_TARIFF_H_

#include <stdint.h>

/**
 * @brief Calcula a tarifa de um tempo estacionado.
 *
 * units = max(1, ceil(dt_ms / unit_ms)); centavos = 200 + (units - 1) * 100.
 *
 * @param dt_ms Tempo desde a passagem completa na entrada.
 * @param unit_ms Tamanho da unidade, em milissegundos (CONFIG_PARKING_TARIFF_UNIT_MS).
 * @param cents Recebe o valor em centavos.
 * @retval 0 Sucesso.
 * @retval -EINVAL Argumento invalido (ponteiro nulo, dt negativo, unidade zero).
 * @retval -ERANGE Mais de 9999 unidades: a faixa deve pedir assistencia.
 */
int tariff_cents(int64_t dt_ms, uint32_t unit_ms, uint32_t *cents);

#endif /* STUDENT_TARIFF_H_ */
