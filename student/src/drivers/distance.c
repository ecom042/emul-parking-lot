#include <errno.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>

#include "distance.h"
#include "equipment_types.h"

#define ENTRY_DISTANCE DT_ALIAS(entry_distance)
#define EXIT_DISTANCE DT_ALIAS(exit_distance)

BUILD_ASSERT(DT_SAME_NODE(DT_PARENT(ENTRY_DISTANCE), DT_PARENT(EXIT_DISTANCE)));

static const struct device *const adc = DEVICE_DT_GET(DT_PARENT(ENTRY_DISTANCE));
static const struct adc_channel_cfg channels[] = {
	ADC_CHANNEL_CFG_DT(ENTRY_DISTANCE),
	ADC_CHANNEL_CFG_DT(EXIT_DISTANCE),
};

/* The async driver retains the sequence, sample buffer and signal until completion. */
static struct {
	struct k_poll_signal signal;
	struct adc_sequence sequence;
	uint16_t buffer;
	struct distance_sample samples[PARKING_LANES];
	int64_t submitted_at;
	unsigned int lane;
	bool pending;
	bool fault;
} acquisition;

int distance_init(void)
{
	if (!device_is_ready(adc)) {
		acquisition.fault = true;
		return -ENODEV;
	}
	for (unsigned int lane = 0; lane < PARKING_LANES; ++lane) {
		int rc = adc_channel_setup(adc, &channels[lane]);

		if (rc < 0) {
			acquisition.fault = true;
			return rc;
		}
	}
	k_poll_signal_init(&acquisition.signal);
	acquisition.sequence = (struct adc_sequence) {
		.buffer = &acquisition.buffer,
		.buffer_size = sizeof(acquisition.buffer),
		.resolution = 12,
	};
	return 0;
}

static bool complete(int64_t now)
{
	unsigned int signaled;
	int result;

	k_poll_signal_check(&acquisition.signal, &signaled, &result);
	if (now - acquisition.submitted_at >= PARKING_ADC_DEADLINE_MS) {
		acquisition.fault = true;
		return false;
	}
	if (!signaled) {
		return false;
	}
	acquisition.pending = false;
	if (result != 0 || acquisition.buffer > 4095) {
		acquisition.fault = true;
		return false;
	}
	acquisition.samples[acquisition.lane] = (struct distance_sample) {
		.observed_at = now,
		.code = acquisition.buffer,
		.valid = true,
	};
	acquisition.lane = (acquisition.lane + 1) % PARKING_LANES;
	return true;
}

void distance_step(int64_t now)
{
	if (acquisition.fault || (acquisition.pending && !complete(now))) {
		return;
	}
	k_poll_signal_reset(&acquisition.signal);
	acquisition.sequence.channels = BIT(channels[acquisition.lane].channel_id);
	acquisition.submitted_at = now;
	if (adc_read_async(adc, &acquisition.sequence, &acquisition.signal) < 0) {
		acquisition.fault = true;
		return;
	}
	acquisition.pending = true;
}

bool distance_faulted(void)
{
	return acquisition.fault;
}

bool distance_read(unsigned int lane, int64_t now, uint16_t *code)
{
	const struct distance_sample *sample = &acquisition.samples[lane];

	if (acquisition.fault || !sample->valid ||
	    now - sample->observed_at >= PARKING_ADC_DEADLINE_MS) {
		return false;
	}
	*code = sample->code;
	return true;
}
