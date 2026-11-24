/* Falsos do FFF para as funções de hw.h usadas por display.c e adc_sampler.c. */
#ifndef FAKES_H_
#define FAKES_H_

#include <zephyr/fff.h>

#include "hw.h"

DECLARE_FAKE_VALUE_FUNC(int, hw_disp_reset, bool);
DECLARE_FAKE_VALUE_FUNC(int, hw_disp_write, bool, uint8_t *, size_t);
DECLARE_FAKE_VOID_FUNC(hw_sleep_ms, int32_t);
DECLARE_FAKE_VALUE_FUNC(int, hw_adc_start, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, hw_adc_check, uint16_t *);

/** Zera todos os falsos e o histórico. */
void fakes_reset(void);

#endif
