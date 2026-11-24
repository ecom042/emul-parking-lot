/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file hw_adc.c
 * @brief ADC4 assincrono com um unico dono: a thread de controle.
 *
 * Uma conversao por vez, alternando o canal 17 (entrada) e o 18 (saida).
 * Sequencia, buffer e sinal sao estaticos: uma conclusao atrasada escreve
 * sempre em memoria valida. A conclusao e consultada com k_poll_signal_check,
 * sem bloquear; a que nao chega, ou chega com ADC_TIMEOUT_MS ou mais, trava a
 * aquisicao ate o reset.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "hw_adc.h"
#include "parking_limits.h"

#define ADC_NODE      DT_PARENT(DT_ALIAS(entry_distance))
#define ADC_BITS      12
#define ADC_CODE_MAX  ((1U << ADC_BITS) - 1U)

BUILD_ASSERT(DT_SAME_NODE(ADC_NODE, DT_PARENT(DT_ALIAS(exit_distance))),
	     "as duas faixas usam o mesmo ADC4");

static const struct adc_channel_cfg channels[LANE_COUNT] = {
	[LANE_ENTRY] = ADC_CHANNEL_CFG_DT(DT_ALIAS(entry_distance)),
	[LANE_EXIT] = ADC_CHANNEL_CFG_DT(DT_ALIAS(exit_distance)),
};

static struct {
	const struct device *dev;
	struct adc_sequence seq;
	struct k_poll_signal done;
	uint16_t buf;
	enum lane_id lane;  /* canal da conversao pendente ou da proxima */
	bool pending;       /* ha uma conversao do driver em andamento */
	bool fault;         /* travado ate o reset */
	int fault_rc;
	int64_t submit_ms;
} adc = {
	.dev = DEVICE_DT_GET(ADC_NODE),
};

static int latch(int rc)
{
	adc.fault = true;
	adc.fault_rc = rc;
	return rc;
}

int hw_adc_init(void)
{
	if (!device_is_ready(adc.dev)) {
		return latch(-ENODEV);
	}
	for (size_t i = 0; i < ARRAY_SIZE(channels); i++) {
		int rc = adc_channel_setup(adc.dev, &channels[i]);

		if (rc < 0) {
			return latch(rc);
		}
	}
	k_poll_signal_init(&adc.done);
	adc.seq.buffer = &adc.buf;
	adc.seq.buffer_size = sizeof(adc.buf);
	adc.seq.resolution = ADC_BITS;
	adc.lane = LANE_ENTRY;
	adc.pending = false;
	return 0;
}

static int submit(int64_t now_ms)
{
	int rc;

	adc.seq.channels = BIT(channels[adc.lane].channel_id);
	k_poll_signal_reset(&adc.done);
	rc = adc_read_async(adc.dev, &adc.seq, &adc.done);
	if (rc < 0) {
		return latch(rc);
	}
	adc.pending = true;
	adc.submit_ms = now_ms;
	return 0;
}

int hw_adc_poll(int64_t now_ms, struct hw_adc_sample *out)
{
	int got = 0;

	if (adc.fault) {
		return adc.fault_rc;
	}

	if (adc.pending) {
		unsigned int signaled;
		int result;

		k_poll_signal_check(&adc.done, &signaled, &result);
		/* Pronta ou nao, a conversao tem de ser vista antes do prazo. */
		if (now_ms - adc.submit_ms >= ADC_TIMEOUT_MS) {
			return latch(-ETIMEDOUT);
		}
		if (signaled == 0U) {
			return 0;
		}
		if (result < 0) {
			return latch(result);
		}
		if (adc.buf > ADC_CODE_MAX) {
			return latch(-ERANGE);
		}
		out->lane = adc.lane;
		out->code = adc.buf;
		adc.pending = false;
		adc.lane = (adc.lane == LANE_ENTRY) ? LANE_EXIT : LANE_ENTRY;
		got = 1;
	}

	int rc = submit(now_ms);

	return (rc < 0) ? rc : got;
}
