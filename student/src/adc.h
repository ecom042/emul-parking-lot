/*
 * adc.h — Asynchronous ADC4 acquisition manager
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ADC_H
#define ADC_H

#include <stdbool.h>
#include <stdint.h>
#include "hw.h"

/* ADC distance thresholds (raw 12-bit codes) */
#define ADC_THRESHOLD_PRESENT  683   /* ~1000 mm: car present */
#define ADC_THRESHOLD_DEPARTED 1024  /* ~1500 mm: car departed */
#define ADC_DEADLINE_MS        100   /* max time to complete a read */

typedef struct {
	lane_id_t lane;
	bool pending;         /* A read is in progress */
	bool valid;           /* Last sample is valid */
	bool fault;           /* ADC fault latched (until reset) */
	uint16_t last_raw;    /* Last valid raw code */
	int64_t submit_ms;    /* When the current read was submitted */
} adc_ctx_t;

/* Initialize ADC context. */
void adc_ctx_init(adc_ctx_t *ctx, lane_id_t lane);

/* Reset ADC context (clears fault). */
void adc_ctx_reset(adc_ctx_t *ctx);

/* Submit a new read request (if not already pending and not faulted). */
int adc_submit(adc_ctx_t *ctx);

/* Check for completion (non-blocking). Returns 0 if completed, -EAGAIN
 * if still pending, negative if error/fault. */
int adc_poll(adc_ctx_t *ctx, int64_t now_ms);

/* Check if distance indicates car present (< threshold). */
bool adc_is_car_present(const adc_ctx_t *ctx);

/* Check if distance indicates car departed (> threshold). */
bool adc_is_car_departed(const adc_ctx_t *ctx);

#endif /* ADC_H */
