/**
 * @file distance.c
 * @brief Aquisição alternada dos sensores de distância no ADC4.
 */

#include "distance.h"

#include <errno.h>
#include <string.h>

/** @brief Última amostra de uma faixa. */
struct sample {
	uint16_t raw;  /**< Código de 12 bits. */
	int64_t at_ms; /**< Quando a conversão foi submetida. */
	bool valid;    /**< Já houve alguma amostra. */
};

static struct {
	struct sample samples[HW_LANE_COUNT];
	enum hw_lane lane;  /**< Faixa da conversão pendente ou da próxima. */
	bool pending;       /**< Há conversão submetida e ainda não observada. */
	int64_t started_ms; /**< Quando a conversão pendente foi submetida. */
	bool faulted;       /**< Falha travada até o reset. */
} adc;

void distance_init(void)
{
	memset(&adc, 0, sizeof(adc));
}

void distance_service(int64_t now_ms)
{
	uint16_t raw = 0;
	int err;

	if (adc.faulted) {
		return;
	}
	if (adc.pending) {
		err = hw_adc_poll(&raw);
		if (err == -EAGAIN && now_ms - adc.started_ms < DISTANCE_DEADLINE_MS) {
			return;
		}
		adc.pending = false;
		if (err < 0 || raw > DISTANCE_MAX_CODE ||
		    now_ms - adc.started_ms >= DISTANCE_DEADLINE_MS) {
			adc.faulted = true;
			return;
		}
		adc.samples[adc.lane] = (struct sample){.raw = raw, .at_ms = adc.started_ms, .valid = true};
		adc.lane = adc.lane == HW_LANE_ENTRY ? HW_LANE_EXIT : HW_LANE_ENTRY;
	}
	if (hw_adc_start(adc.lane) < 0) {
		adc.faulted = true;
		return;
	}
	adc.pending = true;
	adc.started_ms = now_ms;
}

int distance_get(enum hw_lane lane, int64_t now_ms, uint16_t *raw)
{
	const struct sample *s = &adc.samples[lane];

	if (adc.faulted) {
		return -EIO;
	}
	if (!s->valid || now_ms - s->at_ms > DISTANCE_FRESH_MS) {
		return -ENODATA;
	}
	*raw = s->raw;
	return 0;
}

bool distance_faulted(void)
{
	return adc.faulted;
}
