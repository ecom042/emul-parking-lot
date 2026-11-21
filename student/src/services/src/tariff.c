#include "services/tariff.h"

#include <errno.h>

#define FIRST_UNIT_CENTS 200 /**< the first started unit */
#define NEXT_UNIT_CENTS  100 /**< each additional started unit */

/* Started units, at least one. Written without (ms + unit - 1), which could
 * overflow for a huge stay.
 */
static int64_t started_units(int64_t parked_ms)
{
	const int64_t unit_ms = CONFIG_PARKING_TARIFF_UNIT_MS;
	int64_t units = parked_ms / unit_ms + (parked_ms % unit_ms != 0);

	return (units < 1) ? 1 : units;
}

int tariff_cents(int64_t parked_ms, uint32_t *cents)
{
	int64_t units = started_units(parked_ms);

	if (units > TARIFF_MAX_UNITS) {
		return -ERANGE;
	}
	*cents = (uint32_t)(FIRST_UNIT_CENTS + (units - 1) * NEXT_UNIT_CENTS);
	return 0;
}
