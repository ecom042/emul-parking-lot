/*
 * adc.h — Single-owner alternating asynchronous ADC4 manager (REQ-03, REQ-10)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PARKING_ADC_H_
#define PARKING_ADC_H_

#include <stdbool.h>
#include <stdint.h>

#include "hw.h"

#define ADC_CODE_PRESENT_MAX  683U   /* raw < 683 (~1000 mm): vehicle present */
#define ADC_CODE_DEPARTED_MIN 1024U  /* raw > 1024 (~1500 mm): vehicle departed */
#define ADC_MAX_RAW           4095
#define ADC_DEADLINE_MS       100    /* completion must be observed before +100 ms */

typedef struct {
	uint16_t raw;
	int64_t timestamp_ms;
	bool valid;
} adc_sample_t;

typedef struct {
	adc_sample_t samples[LANE_COUNT];
	lane_id_t active_lane;
	int64_t submit_ms;
	bool pending;
	bool fault;
} adc_mgr_t;

int adc_mgr_init(adc_mgr_t *mgr);
int adc_mgr_service(adc_mgr_t *mgr, int64_t now_ms);
bool adc_mgr_is_faulted(const adc_mgr_t *mgr);
bool adc_mgr_has_fresh(const adc_mgr_t *mgr, lane_id_t lane, int64_t now_ms);
bool adc_mgr_car_present(const adc_mgr_t *mgr, lane_id_t lane, int64_t now_ms);
bool adc_mgr_car_departed(const adc_mgr_t *mgr, lane_id_t lane, int64_t now_ms);

#endif /* PARKING_ADC_H_ */
