#include <errno.h>
#include <string.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, setup_channel, const struct device *, const struct adc_channel_cfg *);
FAKE_VALUE_FUNC(int, submit_read, const struct device *, const struct adc_sequence *,
		struct k_poll_signal *);
FAKE_VALUE_FUNC(int64_t, uptime_ms);
FAKE_VALUE_FUNC(int32_t, wait_for_poll, k_timeout_t);

/* Compile the production worker with deterministic I/O and time in this suite.
 * Including its source also lets the fixture join the worker and reset it between tests.
 */
#define adc_channel_setup setup_channel
#define adc_read_async submit_read
#define k_uptime_get uptime_ms
#define k_sleep wait_for_poll
#include "../../../src/parking_adc.c"
#undef k_sleep
#undef k_uptime_get
#undef adc_read_async
#undef adc_channel_setup

#define ENTRY_CHANNEL 17
#define EXIT_CHANNEL 18
#define TEST_RAW 2048

static struct k_sem wake;
static bool worker_started;
static int64_t now_ms;
static int64_t observed_after_ms;
static bool signal_on_poll;
static int conversion_result;
static uint16_t conversion_raw;

static struct parking_inputs snapshot(enum parking_lane_id lane)
{
	struct parking_inputs inputs = {0};

	parking_adc_sample(lane, &inputs);
	return inputs;
}

static int64_t clock_now(void)
{
	return now_ms;
}

static void complete_read(uint16_t value, int result)
{
	uint16_t *buffer = submit_read_fake.arg1_val->buffer;

	*buffer = value;
	zassert_ok(k_poll_signal_raise(submit_read_fake.arg2_val, result));
}

static int32_t advance_to_observation(k_timeout_t timeout)
{
	ARG_UNUSED(timeout);
	zassert_equal(wait_for_poll_fake.call_count, 1);
	zassert_equal(submit_read_fake.call_count, 1);
	zassert_false(snapshot(PARKING_ENTRY).distance.valid);
	zassert_false(snapshot(PARKING_EXIT).distance.valid);
	zassert_false(snapshot(PARKING_EXIT).adc_fault);
	zassert_equal(k_sem_take(&wake, K_NO_WAIT), -EBUSY);

	now_ms += observed_after_ms;
	if (signal_on_poll) {
		complete_read(conversion_raw, conversion_result);
	}
	return 0;
}

static int check_submission(const struct device *dev, const struct adc_sequence *request,
			    struct k_poll_signal *signal)
{
	zassert_equal_ptr(dev, adc4);
	zassert_not_null(request->buffer);
	zassert_equal(request->buffer_size, sizeof(uint16_t));
	zassert_equal(request->resolution, 12);
	zassert_not_null(signal);
	if (submit_read_fake.call_count == 1) {
		zassert_equal(request->channels, BIT(ENTRY_CHANNEL));
		return 0;
	}

	zassert_equal(submit_read_fake.call_count, 2);
	zassert_equal(request->channels, BIT(EXIT_CHANNEL));
	/* The exit request can start only after the entry sample is published. */
	struct parking_inputs entry = snapshot(PARKING_ENTRY);
	struct parking_inputs exit = snapshot(PARKING_EXIT);

	zassert_true(entry.distance.valid);
	zassert_equal(entry.distance.raw, TEST_RAW);
	zassert_equal(entry.distance.sampled_ms, 1000);
	zassert_false(entry.adc_fault);
	zassert_false(exit.distance.valid);
	zassert_ok(k_sem_take(&wake, K_NO_WAIT));
	/* End the worker after checking the next submission. */
	return -EIO;
}

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(&samples, 0, sizeof(samples));
	zassert_ok(k_sem_init(&wake, 0, 1));
	worker_started = false;
	now_ms = 1000;
	observed_after_ms = 99;
	signal_on_poll = true;
	conversion_result = 0;
	conversion_raw = TEST_RAW;
	RESET_FAKE(setup_channel);
	RESET_FAKE(submit_read);
	RESET_FAKE(uptime_ms);
	RESET_FAKE(wait_for_poll);
	FFF_RESET_HISTORY();
	submit_read_fake.custom_fake = check_submission;
	uptime_ms_fake.custom_fake = clock_now;
	wait_for_poll_fake.custom_fake = advance_to_observation;
}

static void after_each(void *fixture)
{
	ARG_UNUSED(fixture);
	if (worker_started) {
		k_thread_abort(&adc_thread);
	}
}

static void run_worker(void)
{
	worker_started = true;
	parking_adc_init(&wake);
	zassert_ok(k_thread_join(&adc_thread, K_SECONDS(1)));
	zassert_equal(setup_channel_fake.call_count, 2);
	zassert_equal(setup_channel_fake.arg1_history[0]->channel_id, ENTRY_CHANNEL);
	zassert_equal(setup_channel_fake.arg1_history[1]->channel_id, EXIT_CHANNEL);
	zassert_ok(k_sem_take(&wake, K_NO_WAIT));
}

static void assert_fault_without_sample(void)
{
	for (int lane = 0; lane < PARKING_LANE_COUNT; lane++) {
		struct parking_inputs inputs = snapshot(lane);

		zassert_true(inputs.adc_fault, "lane %d", lane);
		zassert_false(inputs.distance.valid, "lane %d", lane);
	}
	zassert_equal(submit_read_fake.call_count, 1);
}

ZTEST_SUITE(parking_adc, NULL, NULL, before_each, after_each, NULL);

ZTEST(parking_adc, test_observed_at_99_ms_publishes_then_switches_channel)
{
	run_worker();
	zassert_equal(submit_read_fake.call_count, 2);
}

ZTEST(parking_adc, test_observed_at_100_ms_latches_fault)
{
	observed_after_ms = 100;
	run_worker();
	assert_fault_without_sample();
}

ZTEST(parking_adc, test_observed_after_100_ms_latches_fault)
{
	observed_after_ms = 101;
	run_worker();
	assert_fault_without_sample();
}

ZTEST(parking_adc, test_late_completion_after_timeout_cannot_publish_or_resubmit)
{
	observed_after_ms = 100;
	signal_on_poll = false;
	run_worker();
	assert_fault_without_sample();

	now_ms += 25;
	complete_read(TEST_RAW, 0);
	assert_fault_without_sample();
	zassert_equal(k_sem_take(&wake, K_NO_WAIT), -EBUSY);
}

ZTEST(parking_adc, test_submission_error_faults_both_lanes)
{
	submit_read_fake.custom_fake = NULL;
	submit_read_fake.return_val = -EIO;
	run_worker();
	assert_fault_without_sample();
	zassert_equal(wait_for_poll_fake.call_count, 0);
}

ZTEST(parking_adc, test_completion_error_faults_both_lanes)
{
	conversion_result = -EIO;
	run_worker();
	assert_fault_without_sample();
}

ZTEST(parking_adc, test_out_of_range_sample_faults_both_lanes)
{
	conversion_raw = 4096;
	run_worker();
	assert_fault_without_sample();
}
