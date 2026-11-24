/*
 * Tarifa (REQ-12). Ver tariff.h.
 */
#include "tariff.h"

#include <errno.h>
#include <stdio.h>

#include "config.h"

int tariff_compute(int64_t elapsed_ms, uint32_t unit_ms, uint32_t *centavos)
{
	int64_t units;

	if (unit_ms == 0U) {
		return -EINVAL;
	}
	if (elapsed_ms < 0) {
		elapsed_ms = 0;
	}
	/* Divisão inteira arredondada para cima; no mínimo uma unidade. */
	units = (elapsed_ms + (int64_t)unit_ms - 1) / (int64_t)unit_ms;
	if (units < 1) {
		units = 1;
	}
	if (units > TARIFF_MAX_UNITS) {
		return -ERANGE;
	}
	*centavos = TARIFF_FIRST_CENTS + (uint32_t)(units - 1) * TARIFF_EXTRA_CENTS;
	return 0;
}

int tariff_format_brl(char *out, size_t size, uint32_t centavos)
{
	int n = snprintf(out, size, "BRL %u.%02u", (unsigned int)(centavos / 100U),
			 (unsigned int)(centavos % 100U));

	/* n negativo (erro) vira um valor enorme no cast e também cai aqui. */
	if ((size_t)n >= size) {
		return -EINVAL;
	}
	return n;
}
