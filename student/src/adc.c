/*
 * adc.c — Asynchronous ADC4 acquisition manager
 *
 * Owns the async ADC read cycle: submit → poll → read result.
 * Only one outstanding request at a time per lane.
 * Deadline enforcement: 100ms per read.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "adc.h"
#include <zephyr/kernel.h>

void adc_ctx_init(adc_ctx_t *ctx, lane_id_t lane)
{
	ctx->lane = lane;
	ctx->pending = false;
	ctx->valid = false;
	ctx->fault = false;
	ctx->last_raw = 4095; /* Default: far away */
	ctx->submit_ms = 0;
}

void adc_ctx_reset(adc_ctx_t *ctx)
{
	lane_id_t lane = ctx->lane;
	adc_ctx_init(ctx, lane);
}

int adc_submit(adc_ctx_t *ctx)
{
	if (ctx->fault) {
		return -EIO;
	}
	if (ctx->pending) {
		return -EBUSY;
	}

	int rc = hw_adc_read_async(ctx->lane);
	if (rc != 0) {
		ctx->fault = true;
		return rc;
	}

	ctx->pending = true;
	ctx->submit_ms = k_uptime_get();
	return 0;
}

int adc_poll(adc_ctx_t *ctx, int64_t now_ms)
{
	if (ctx->fault) {
		return -EIO;
	}
	if (!ctx->pending) {
		return -EINVAL;
	}

	/* Check deadline */
	if (now_ms >= ctx->submit_ms + ADC_DEADLINE_MS) {
		ctx->fault = true;
		ctx->pending = false;
		return -ETIMEDOUT;
	}

	uint16_t raw;
	int rc = hw_adc_check_completion(ctx->lane, &raw);
	if (rc == -EAGAIN) {
		return -EAGAIN; /* Still pending */
	}
	if (rc != 0) {
		ctx->fault = true;
		ctx->pending = false;
		return rc;
	}

	/* Success */
	ctx->pending = false;
	ctx->valid = true;
	ctx->last_raw = raw;
	return 0;
}

bool adc_is_car_present(const adc_ctx_t *ctx)
{
	return ctx->valid && ctx->last_raw < ADC_THRESHOLD_PRESENT;
}

bool adc_is_car_departed(const adc_ctx_t *ctx)
{
	return ctx->valid && ctx->last_raw > ADC_THRESHOLD_DEPARTED;
}
