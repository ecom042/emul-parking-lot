#ifndef PARKING_PAYMENT_DISPLAY_H
#define PARKING_PAYMENT_DISPLAY_H

#include "parking.h"

/* Single owner: initialize and draw from the exit worker. Propagate hardware
 * faults to the caller before starting any charge.
 */
int payment_display_init(void);
int payment_display_show_plate(const char plate[PARKING_PLATE_SIZE]);
int payment_display_show_amount(uint32_t cents);

#endif
