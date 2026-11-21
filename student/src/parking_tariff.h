#ifndef PARKING_TARIFF_H
#define PARKING_TARIFF_H

#include <stdint.h>

/* Timestamps use Zephyr uptime in milliseconds. The amount changes only on
 * success. An out-of-range stay returns -ERANGE.
 */
int parking_tariff(int64_t entered_ms, int64_t now_ms, uint32_t *cents);

#endif
