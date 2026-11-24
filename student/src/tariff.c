/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file tariff.c
 * @brief Tarifa inteira: 200 centavos na primeira unidade, 100 por unidade extra.
 */

#include <errno.h>
#include <stddef.h>

#include <zephyr/kernel.h>

#include "parking_limits.h"
#include "tariff.h"

int tariff_cents(int64_t dt_ms, uint32_t unit_ms, uint32_t *cents)
{
	uint64_t dt;
	uint64_t units;
	uint64_t amount;

	if (cents == NULL || unit_ms == 0 || dt_ms < 0) {
		return -EINVAL;
	}

	/* Teto em 64 bits, sem float e sem overflow: dt <= INT64_MAX. */
	dt = (uint64_t)dt_ms;
	units = (dt + unit_ms - 1U) / unit_ms;
	if (units < 1U) {
		units = 1U;
	}
	if (units > TARIFF_UNITS_MAX) {
		return -ERANGE;
	}

	amount = TARIFF_BASE + (units - 1U) * TARIFF_STEP;
	if (amount > UINT32_MAX) {
		return -ERANGE;
	}

	*cents = (uint32_t)amount;
	return 0;
}
