/*
 * adc.c — Single-owner alternating asynchronous ADC4 manager implementation
 *
 * Owns ADC4 from one place, issuing at most one asynchronous single-channel
 * conversion at a time and alternating between channel 17 (entry) and
 * channel 18 (exit). Any submission, completion, range or >=100 ms deadline
 * error latches fault=true until machine reset without reusing buffers.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "adc.h"

#include <errno.h>
#include <string.h>

int adc_mgr_init(adc_mgr_t *mgr)
{
	int rc;

	(void)memset(mgr, 0, sizeof(*mgr));
	mgr->samples[LANE_ENTRY].raw = (uint16_t)ADC_MAX_RAW;
	mgr->samples[LANE_EXIT].raw = (uint16_t)ADC_MAX_RAW;
	mgr->active_lane = LANE_ENTRY;

	if (!hw_adc_is_ready()) {
		mgr->fault = true;
		return -ENODEV;
	}

	rc = hw_adc_setup_channel(LANE_ENTRY);
	if (rc != 0) {
		mgr->fault = true;
		return rc;
	}

	rc = hw_adc_setup_channel(LANE_EXIT);
	if (rc != 0) {
		mgr->fault = true;
		return rc;
	}

	return 0;
}

int adc_mgr_service(adc_mgr_t *mgr, int64_t now_ms)
{
	int rc;

	if (mgr->fault) {
		return -EIO;
	}

	if (mgr->pending) {
		unsigned int signaled = 0U;
		int result = 0;
		int16_t raw_sample = 0;

		if ((now_ms - mgr->submit_ms) >= ADC_DEADLINE_MS) {
			mgr->fault = true;
			return -ETIMEDOUT;
		}

		hw_adc_check_async(&signaled, &result, &raw_sample);
		if (signaled == 0U) {
			return 0;
		}

		mgr->pending = false;
		if (result != 0 || raw_sample < 0 || raw_sample > ADC_MAX_RAW) {
			mgr->fault = true;
			return -ERANGE;
		}

		mgr->samples[mgr->active_lane].raw = (uint16_t)raw_sample;
		mgr->samples[mgr->active_lane].timestamp_ms = now_ms;
		mgr->samples[mgr->active_lane].valid = true;

		mgr->active_lane = (mgr->active_lane == LANE_ENTRY) ?
				   LANE_EXIT : LANE_ENTRY;
	}

	rc = hw_adc_start_async(mgr->active_lane);
	if (rc != 0) {
		mgr->fault = true;
		return rc;
	}

	mgr->pending = true;
	mgr->submit_ms = now_ms;
	return 0;
}

bool adc_mgr_is_faulted(const adc_mgr_t *mgr)
{
	return mgr->fault;
}

bool adc_mgr_has_fresh(const adc_mgr_t *mgr, lane_id_t lane, int64_t now_ms)
{
	const adc_sample_t *s = &mgr->samples[lane];

	return !mgr->fault && s->valid &&
	       ((now_ms - s->timestamp_ms) < ADC_DEADLINE_MS);
}

bool adc_mgr_car_present(const adc_mgr_t *mgr, lane_id_t lane, int64_t now_ms)
{
	return adc_mgr_has_fresh(mgr, lane, now_ms) &&
	       (mgr->samples[lane].raw < ADC_CODE_PRESENT_MAX);
}

bool adc_mgr_car_departed(const adc_mgr_t *mgr, lane_id_t lane, int64_t now_ms)
{
	return adc_mgr_has_fresh(mgr, lane, now_ms) &&
	       (mgr->samples[lane].raw > ADC_CODE_DEPARTED_MIN);
}
