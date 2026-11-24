/* Tarifa do estacionamento (REQ-12). Veja tariff.c. */
#ifndef TARIFF_H_
#define TARIFF_H_

#include <stdint.h>

#define TARIFF_BASE_CENTAVOS 200
#define TARIFF_STEP_CENTAVOS 100
#define TARIFF_MAX_UNITS 9999

/**
 * Calcula o valor em centavos para um tempo estacionado.
 *
 * @param elapsed_ms tempo virtual desde a passagem na entrada (>= 0).
 * @param unit_ms    CONFIG_PARKING_TARIFF_UNIT_MS.
 * @param amount     saída: valor em centavos.
 * @return 0; -EINVAL (argumento inválido); -ERANGE (mais de 9999 unidades).
 */
int tariff_amount(int64_t elapsed_ms, uint32_t unit_ms, uint32_t *amount);

#endif /* TARIFF_H_ */
