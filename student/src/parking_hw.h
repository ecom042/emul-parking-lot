#ifndef PARKING_HW_H
#define PARKING_HW_H

#include "lane.h"

struct k_sem;

extern const struct parking_io parking_gpio_io;

void parking_gpio_init(struct k_sem *wake);
void parking_gpio_sample(enum parking_lane_id id, struct parking_inputs *inputs);

void parking_adc_init(struct k_sem *wake);
void parking_adc_sample(enum parking_lane_id id, struct parking_inputs *inputs);

#endif
