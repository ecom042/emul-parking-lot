/**
 * @file tariff.c
 * @brief Tarifa do estacionamento em centavos inteiros.
 */

#include "tariff.h"

#include <errno.h>
#include <stddef.h>

int tariff_compute(int64_t elapsed_ms, uint32_t unit_ms, uint32_t *amount)
{
	int64_t units;

	if (elapsed_ms < 0 || unit_ms == 0U || amount == NULL) {
		return -EINVAL;
	}
	/* Teto sem somar unit_ms - 1, que estouraria com elapsed_ms perto de INT64_MAX. */
	units = elapsed_ms / unit_ms + (elapsed_ms % unit_ms != 0 ? 1 : 0);
	if (units < 1) {
		units = 1;
	}
	if (units > TARIFF_MAX_UNITS) {
		return -ERANGE;
	}
	*amount = TARIFF_BASE_CENTAVOS + (uint32_t)(units - 1) * TARIFF_EXTRA_CENTAVOS;
	return 0;
}
