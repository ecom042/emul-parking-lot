#include "hw/adc4.h"

#include <errno.h>

#include <zephyr/kernel.h>

/* Static: they must outlive a completion that arrives arbitrarily late. */
static const struct device *adc_dev;
static uint16_t sample;
static struct adc_sequence sequence;
static struct k_poll_signal done;
static bool busy;

int adc4_init(const struct device *adc, const struct adc_channel_cfg *cfgs, size_t count)
{
	if (!device_is_ready(adc)) {
		return -ENODEV;
	}
	for (size_t i = 0; i < count; i++) {
		int rc = adc_channel_setup(adc, &cfgs[i]);

		if (rc != 0) {
			return rc;
		}
	}
	adc_dev = adc;
	busy = false;
	k_poll_signal_init(&done);
	return 0;
}

int adc4_start(uint8_t channel)
{
	if (busy) {
		return -EBUSY;
	}
	sequence = (struct adc_sequence){
		.channels = BIT(channel),
		.buffer = &sample,
		.buffer_size = sizeof(sample),
		.resolution = 12,
	};
	k_poll_signal_reset(&done);

	int rc = adc_read_async(adc_dev, &sequence, &done);

	if (rc == 0) {
		busy = true;
	}
	return rc;
}

int adc4_poll(uint16_t *code)
{
	unsigned int signaled;
	int result;

	if (!busy) {
		return -EINVAL;
	}
	k_poll_signal_check(&done, &signaled, &result);
	if (!signaled) {
		return -EAGAIN;
	}
	busy = false;
	if (result != 0) {
		return result;
	}
	if (sample > ADC4_MAX_CODE) {
		return -EIO;
	}
	*code = sample;
	return 0;
}

bool adc4_busy(void)
{
	return busy;
}
