/**
 * @file tariff.h
 * @brief Tarifa do estacionamento em centavos inteiros.
 */

#ifndef TARIFF_H_
#define TARIFF_H_

#include <stdint.h>

/** @brief Número máximo de unidades cobráveis; acima disso, assistência. */
#define TARIFF_MAX_UNITS 9999

/** @brief Preço da primeira unidade iniciada, em centavos. */
#define TARIFF_BASE_CENTAVOS 200

/** @brief Preço de cada unidade iniciada depois da primeira, em centavos. */
#define TARIFF_EXTRA_CENTAVOS 100

/**
 * @brief Calcula a tarifa de uma permanência.
 *
 * unidades = max(1, ceil(elapsed_ms / unit_ms));
 * valor = 200 + (unidades - 1) * 100 centavos.
 *
 * @param elapsed_ms Tempo virtual estacionado, desde a passagem na entrada.
 * @param unit_ms Duração de uma unidade (CONFIG_PARKING_TARIFF_UNIT_MS).
 * @param amount Recebe o valor em centavos.
 * @return 0 em sucesso, -EINVAL se elapsed_ms < 0, unit_ms == 0 ou amount é
 *         NULL, -ERANGE se passar de TARIFF_MAX_UNITS unidades.
 */
int tariff_compute(int64_t elapsed_ms, uint32_t unit_ms, uint32_t *amount);

#endif /* TARIFF_H_ */
