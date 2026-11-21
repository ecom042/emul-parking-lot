#ifndef PARKING_SERVICES_TARIFF_H_
#define PARKING_SERVICES_TARIFF_H_

/*
 * Exit tariff (REQ-12): 200 centavos for the first started unit of virtual
 * parking time, then 100 per additional started unit. The unit is
 * CONFIG_PARKING_TARIFF_UNIT_MS. Integer arithmetic only.
 */

#include <stdint.h>

/** Largest number of units charged; beyond it the lane asks for assistance. */
#define TARIFF_MAX_UNITS 9999

/**
 * @brief The amount to charge for a stay.
 * units = max(1, ceil(parked_ms / CONFIG_PARKING_TARIFF_UNIT_MS)),
 * amount = 200 + (units - 1) * 100 centavos (at most 1000000).
 * @param parked_ms virtual time since the completed entry passage.
 * @param[out] cents the amount; untouched on error.
 * @retval 0 on success.
 * @retval -ERANGE more than TARIFF_MAX_UNITS units: request assistance.
 */
int tariff_cents(int64_t parked_ms, uint32_t *cents);

#endif /* PARKING_SERVICES_TARIFF_H_ */
