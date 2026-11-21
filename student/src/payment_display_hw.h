#ifndef PARKING_PAYMENT_DISPLAY_HW_H
#define PARKING_PAYMENT_DISPLAY_HW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

int payment_display_hw_prepare(void);
int payment_display_hw_reset(bool active);
int payment_display_hw_write(bool data, uint8_t *bytes, size_t len);

#endif
