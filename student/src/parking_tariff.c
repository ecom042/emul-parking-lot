#include <errno.h>
#include <stddef.h>

#include "parking_tariff.h"

enum { FIRST_UNIT_CENTS = 200, ADDITIONAL_UNIT_CENTS = 100, MAX_UNITS = 9999 };

int parking_tariff(int64_t entered_ms, int64_t now_ms, uint32_t *cents)
{
	if (cents == NULL || entered_ms < 0 || now_ms < entered_ms) {
		return -EINVAL;
	}

	int64_t elapsed_ms = now_ms - entered_ms;
	int64_t units = elapsed_ms / CONFIG_PARKING_TARIFF_UNIT_MS;

	units += elapsed_ms % CONFIG_PARKING_TARIFF_UNIT_MS != 0;
	if (units > MAX_UNITS) {
		return -ERANGE;
	}
	if (units == 0) {
		units = 1;
	}
	*cents = FIRST_UNIT_CENTS + (uint32_t)(units - 1) * ADDITIONAL_UNIT_CENTS;
	return 0;
}
