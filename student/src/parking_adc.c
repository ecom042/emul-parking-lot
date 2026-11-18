#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>

#include "parking_hw.h"

#define ENTRY_DISTANCE DT_ALIAS(entry_distance)
#define EXIT_DISTANCE DT_ALIAS(exit_distance)

BUILD_ASSERT(DT_SAME_NODE(DT_PARENT(ENTRY_DISTANCE), DT_PARENT(EXIT_DISTANCE)),
	     "Both distance channels must use the same ADC");
BUILD_ASSERT(DT_PROP(ENTRY_DISTANCE, zephyr_resolution) == 12 &&
	     DT_PROP(EXIT_DISTANCE, zephyr_resolution) == 12, "Distance thresholds require 12 bits");

enum { ADC_PERIOD_MS = 10, ADC_DEADLINE_MS = 100, ADC_MAX_RAW = 4095 };

static const struct device *const adc4 = DEVICE_DT_GET(DT_PARENT(ENTRY_DISTANCE));
static const struct adc_channel_cfg channels[PARKING_LANE_COUNT] = {
	[PARKING_ENTRY] = ADC_CHANNEL_CFG_DT(ENTRY_DISTANCE),
	[PARKING_EXIT] = ADC_CHANNEL_CFG_DT(EXIT_DISTANCE),
};

static struct {
	struct k_spinlock lock;
	struct parking_distance distance[PARKING_LANE_COUNT];
	int64_t deadline_ms;
	bool fault;
} samples;

/* The driver can still complete after the worker has exited on timeout. */
static uint16_t raw;
static struct adc_sequence sequence = {
	.buffer = &raw,
	.buffer_size = sizeof(raw),
	.resolution = 12,
};
static struct k_poll_signal conversion_signal;
static struct k_thread adc_thread;
K_THREAD_STACK_DEFINE(adc_stack, 1024);

static void fail(struct k_sem *wake)
{
	k_spinlock_key_t key = k_spin_lock(&samples.lock);
	samples.fault = true;
	k_spin_unlock(&samples.lock, key);
	k_sem_give(wake);
}

static bool arm_deadline(int64_t now_ms)
{
	k_spinlock_key_t key = k_spin_lock(&samples.lock);
	samples.fault |= now_ms >= samples.deadline_ms;
	bool healthy = !samples.fault;
	if (healthy) {
		samples.deadline_ms = now_ms + ADC_DEADLINE_MS;
	}
	k_spin_unlock(&samples.lock, key);
	return healthy;
}

static bool read_distance(enum parking_lane_id lane, int64_t submitted_ms)
{
	sequence.channels = BIT(channels[lane].channel_id);
	k_poll_signal_reset(&conversion_signal);
	if (adc_read_async(adc4, &sequence, &conversion_signal) != 0) {
		return false;
	}

	while (true) {
		unsigned int signaled;
		int result;

		k_spinlock_key_t key = k_spin_lock(&samples.lock);
		k_poll_signal_check(&conversion_signal, &signaled, &result);
		int64_t now_ms = k_uptime_get();
		samples.fault |= now_ms >= samples.deadline_ms ||
				 (signaled && (result != 0 || raw > ADC_MAX_RAW));
		bool healthy = !samples.fault;
		if (healthy && signaled) {
			samples.distance[lane] = (struct parking_distance){
				.raw = raw,
				.sampled_ms = submitted_ms,
				.valid = true,
			};
			/* Watch the worker until it begins the next conversion. */
			samples.deadline_ms = now_ms + ADC_DEADLINE_MS;
		}
		k_spin_unlock(&samples.lock, key);
		if (!healthy || signaled) {
			return healthy;
		}
		k_sleep(K_MSEC(1));
	}
}

static void acquire(void *context, void *unused1, void *unused2)
{
	struct k_sem *wake = context;

	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);
	k_poll_signal_init(&conversion_signal);
	if (!device_is_ready(adc4)) {
		fail(wake);
		return;
	}
	for (int i = 0; i < PARKING_LANE_COUNT; i++) {
		if (adc_channel_setup(adc4, &channels[i]) != 0) {
			fail(wake);
			return;
		}
	}

	enum parking_lane_id lane = PARKING_ENTRY;
	while (true) {
		int64_t submitted_ms = k_uptime_get();

		if (!arm_deadline(submitted_ms) || !read_distance(lane, submitted_ms)) {
			fail(wake);
			return;
		}
		k_sem_give(wake);
		lane = lane == PARKING_ENTRY ? PARKING_EXIT : PARKING_ENTRY;
		int64_t delay_ms = submitted_ms + ADC_PERIOD_MS - k_uptime_get();
		if (delay_ms > 0) {
			k_sleep(K_MSEC(delay_ms));
		}
	}
}

void parking_adc_init(struct k_sem *wake)
{
	samples.deadline_ms = k_uptime_get() + ADC_DEADLINE_MS;
	k_thread_create(&adc_thread, adc_stack, K_THREAD_STACK_SIZEOF(adc_stack),
			acquire, wake, NULL, NULL, K_PRIO_PREEMPT(0), 0, K_NO_WAIT);
}

void parking_adc_sample(enum parking_lane_id id, struct parking_inputs *inputs)
{
	/* The controller can latch a stalled driver call without waiting on the ADC worker. */
	int64_t now_ms = k_uptime_get();
	k_spinlock_key_t key = k_spin_lock(&samples.lock);

	samples.fault |= now_ms >= samples.deadline_ms;
	inputs->distance = samples.distance[id];
	inputs->adc_fault = samples.fault;
	k_spin_unlock(&samples.lock, key);
}
