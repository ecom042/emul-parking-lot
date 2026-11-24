/*
 * Tarifa (REQ-12), só com aritmética inteira:
 *
 *   unidades = max(1, ceil(estadia_ms / unidade_ms))
 *   centavos = 200 + (unidades - 1) * 100
 *
 * A estadia conta desde a passagem de entrada. Acima de TARIFF_MAX_UNITS
 * unidades, não cobra: a faixa pede assistência.
 */
#ifndef PARKING_TARIFF_H
#define PARKING_TARIFF_H

#include <stddef.h>
#include <stdint.h>

/* Primeira unidade e cada unidade adicional, em centavos. */
#define TARIFF_FIRST_CENTS 200U
#define TARIFF_EXTRA_CENTS 100U

/**
 * Calcula o valor de uma estadia.
 * @return 0 com *centavos, -EINVAL (unit_ms = 0) ou -ERANGE (unidades demais).
 */
int tariff_compute(int64_t elapsed_ms, uint32_t unit_ms, uint32_t *centavos);

/**
 * Escreve "BRL <reais>.<centavos com 2 dígitos>", ex.: "BRL 4.00".
 * @return tamanho escrito ou -EINVAL se não couber.
 */
int tariff_format_brl(char *out, size_t size, uint32_t centavos);

#endif /* PARKING_TARIFF_H */
