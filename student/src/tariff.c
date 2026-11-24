/*
 * Tarifa (REQ-12): lógica pura, sem hardware.
 *
 *   unidades = max(1, ceil(tempo_ms / unidade_ms))
 *   valor    = 200 + (unidades - 1) * 100 centavos
 *
 * Exemplo com unidade de 1000 ms: 2500 ms estacionado -> 3 unidades -> 400.
 * Acima de 9999 unidades o valor passaria de 1000000 centavos (limite do
 * terminal), então devolvemos erro e o chamador pede assistência.
 */
#include "tariff.h"

#include <errno.h>

int tariff_amount(int64_t elapsed_ms, uint32_t unit_ms, uint32_t *amount)
{
	if (unit_ms == 0U || elapsed_ms < 0) {
		return -EINVAL;
	}

	/* Divisão arredondada para cima, só com inteiros. */
	int64_t units = (elapsed_ms + (int64_t)unit_ms - 1) / (int64_t)unit_ms;

	if (units < 1) {
		units = 1; /* 0 ms estacionado ainda cobra a primeira unidade. */
	}
	if (units > TARIFF_MAX_UNITS) {
		return -ERANGE;
	}
	*amount = (uint32_t)(TARIFF_BASE_CENTAVOS + (units - 1) * TARIFF_STEP_CENTAVOS);
	return 0;
}
