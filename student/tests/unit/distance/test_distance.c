/*
 * Tests of services/distance with FFF fakes in place of hw/adc4.
 *
 * adc4_poll() plays a configurable result (return code and code). events_post()
 * is the real one from fsm/lanes.c: the tests read the events from the lane's
 * queue. A zbus listener counts the publications and keeps the last sample.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "fsm/lanes.h"
#include "hw/adc4.h"
#include "hw/lane_hw.h"
#include "services/distance.h"
#include "services/events.h"

/* DEFINE_FFF_GLOBALS lives in test_exemplo.c: one per test binary. */
FAKE_VALUE_FUNC(int, adc4_init, const struct device *, const struct adc_channel_cfg *, size_t);
FAKE_VALUE_FUNC(int, adc4_start, uint8_t);
FAKE_VALUE_FUNC(int, adc4_poll, uint16_t *);
FAKE_VALUE_FUNC(bool, adc4_busy);

static int poll_rc;
static uint16_t poll_code;

static int fake_poll(uint16_t *code)
{
	*code = poll_code;
	return poll_rc;
}

/* distance_init() passes a stack array: keep a copy of what it received. */
static struct adc_channel_cfg init_cfgs[LANE_COUNT];

static int fake_init(const struct device *adc, const struct adc_channel_cfg *cfgs, size_t count)
{
	ARG_UNUSED(adc);
	for (size_t i = 0; i < count && i < LANE_COUNT; i++) {
		init_cfgs[i] = cfgs[i];
	}
	return 0;
}

static unsigned int pubs;
static struct distance_sample last;

static void on_sample(const struct zbus_channel *chan)
{
	last = *(const struct distance_sample *)zbus_chan_const_msg(chan);
	pubs++;
}

ZBUS_LISTENER_DEFINE(test_listener, on_sample);
ZBUS_CHAN_ADD_OBS(chan_distance, test_listener, 3);

static unsigned int events_in(enum lane_id lane, enum lane_event ev)
{
	uint8_t e;
	unsigned int n = 0;

	while (k_msgq_get(lanes_get(lane)->queue, &e, K_NO_WAIT) == 0) {
		zassert_equal(e, ev);
		n++;
	}
	return n;
}

static void zero_adc_fakes(void)
{
	RESET_FAKE(adc4_init);
	RESET_FAKE(adc4_start);
	RESET_FAKE(adc4_poll);
	RESET_FAKE(adc4_busy);
	FFF_RESET_HISTORY();
	adc4_init_fake.custom_fake = fake_init;
	adc4_poll_fake.custom_fake = fake_poll;
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);
	zero_adc_fakes();
	poll_rc = -EAGAIN;
	poll_code = 0;
	pubs = 0;
	last = (struct distance_sample){0};
	k_msgq_purge(lanes_get(LANE_ENTRY)->queue);
	k_msgq_purge(lanes_get(LANE_EXIT)->queue);
	zassert_ok(distance_init());
}

ZTEST_SUITE(distance, NULL, NULL, reset, NULL, NULL);

/* Both lanes get exactly one fault event each. */
static void expect_fault_on_both_lanes(void)
{
	zassert_equal(events_in(LANE_ENTRY, EV_ADC_FAULT), 1);
	zassert_equal(events_in(LANE_EXIT, EV_ADC_FAULT), 1);
}

ZTEST(distance, test_init_configures_both_channels)
{
	zassert_equal(adc4_init_fake.call_count, 1);
	zassert_equal_ptr(adc4_init_fake.arg0_val, distance_adc_get());
	zassert_equal(adc4_init_fake.arg2_val, 2);
	zassert_equal(init_cfgs[0].channel_id, 17);
	zassert_equal(init_cfgs[1].channel_id, 18);
}

ZTEST(distance, test_init_error_is_returned_and_latches)
{
	zero_adc_fakes();
	adc4_init_fake.custom_fake = NULL;
	adc4_init_fake.return_val = -ENODEV;
	zassert_equal(distance_init(), -ENODEV);
	distance_poll(0);
	zassert_equal(adc4_start_fake.call_count, 0);
	zassert_equal(events_in(LANE_ENTRY, EV_ADC_FAULT), 0);
	zassert_equal(events_in(LANE_EXIT, EV_ADC_FAULT), 0);
}

ZTEST(distance, test_first_poll_starts_entry_channel)
{
	distance_poll(0);
	zassert_equal(adc4_start_fake.call_count, 1);
	zassert_equal(adc4_start_fake.arg0_val, 17);
	zassert_equal(adc4_poll_fake.call_count, 0);
}

ZTEST(distance, test_pending_before_deadline_does_nothing)
{
	distance_poll(0);
	distance_poll(50);
	zassert_equal(adc4_start_fake.call_count, 1);
	zassert_equal(pubs, 0);
	zassert_equal(events_in(LANE_ENTRY, EV_ADC_FAULT), 0);
	zassert_equal(events_in(LANE_EXIT, EV_ADC_FAULT), 0);
}

ZTEST(distance, test_completion_publishes_and_alternates_lane)
{
	distance_poll(0);
	poll_rc = 0;
	poll_code = 500;
	distance_poll(3);
	zassert_equal(pubs, 1);
	zassert_equal(last.lane, LANE_ENTRY);
	zassert_equal(last.code, 500);
	zassert_equal(last.taken_ms, 3);
	zassert_equal(adc4_start_fake.call_count, 2);
	zassert_equal(adc4_start_fake.arg0_val, 18);

	poll_code = 3000;
	distance_poll(8);
	zassert_equal(pubs, 2);
	zassert_equal(last.lane, LANE_EXIT);
	zassert_equal(last.code, 3000);
	zassert_equal(last.taken_ms, 8);
	zassert_equal(adc4_start_fake.call_count, 3);
	zassert_equal(adc4_start_fake.arg0_val, 17);
}

ZTEST(distance, test_pending_at_99_is_ok_and_at_100_is_a_fault)
{
	distance_poll(0);
	distance_poll(99);
	zassert_equal(events_in(LANE_ENTRY, EV_ADC_FAULT), 0);
	distance_poll(100);
	expect_fault_on_both_lanes();
}

ZTEST(distance, test_completion_observed_at_100_is_a_fault_without_sample)
{
	distance_poll(0);
	poll_rc = 0;
	poll_code = 500;
	distance_poll(100);
	zassert_equal(pubs, 0);
	expect_fault_on_both_lanes();
}

ZTEST(distance, test_completion_observed_at_99_is_published)
{
	distance_poll(0);
	poll_rc = 0;
	poll_code = 500;
	distance_poll(99);
	zassert_equal(pubs, 1);
	zassert_equal(last.taken_ms, 99);
	zassert_equal(events_in(LANE_ENTRY, EV_ADC_FAULT), 0);
}

ZTEST(distance, test_poll_eio_faults_both_lanes_without_sample)
{
	distance_poll(0);
	poll_rc = -EIO;
	distance_poll(1);
	zassert_equal(pubs, 0);
	expect_fault_on_both_lanes();
}

ZTEST(distance, test_poll_driver_error_faults)
{
	distance_poll(0);
	poll_rc = -EFAULT;
	distance_poll(1);
	zassert_equal(pubs, 0);
	expect_fault_on_both_lanes();
}

ZTEST(distance, test_start_error_on_first_start_faults)
{
	adc4_start_fake.return_val = -EBUSY;
	distance_poll(0);
	expect_fault_on_both_lanes();
}

ZTEST(distance, test_start_error_after_a_completion_faults)
{
	distance_poll(0);
	poll_rc = 0;
	poll_code = 700;
	adc4_start_fake.return_val = -EIO;
	distance_poll(2);
	zassert_equal(pubs, 1);
	expect_fault_on_both_lanes();
}

ZTEST(distance, test_fault_is_latched)
{
	distance_poll(0);
	poll_rc = -EIO;
	distance_poll(1);
	expect_fault_on_both_lanes();
	poll_rc = 0;
	for (int64_t t = 2; t <= 500; t += 7) {
		distance_poll(t);
	}
	zassert_equal(adc4_poll_fake.call_count, 1);
	zassert_equal(adc4_start_fake.call_count, 1);
	zassert_equal(pubs, 0);
	zassert_equal(events_in(LANE_ENTRY, EV_ADC_FAULT), 0);
	zassert_equal(events_in(LANE_EXIT, EV_ADC_FAULT), 0);
}

ZTEST(distance, test_work_polls_periodically)
{
	poll_rc = 0;
	poll_code = 1000;
	distance_start();
	k_msleep(30);
	zassert_true(adc4_start_fake.call_count >= 3);
	zassert_equal(adc4_start_fake.arg0_history[0], 17);
	zassert_equal(adc4_start_fake.arg0_history[1], 18);
	zassert_equal(adc4_start_fake.arg0_history[2], 17);
	zassert_true(pubs >= 2);
	zassert_true(last.taken_ms > 0);
	zassert_true(last.taken_ms <= k_uptime_get());
	/* Stop the work for the next test. */
	zassert_ok(distance_init());
}

ZTEST(distance, test_work_faults_once_when_never_completing)
{
	distance_start();
	k_msleep(150);
	zassert_equal(adc4_start_fake.call_count, 1);
	expect_fault_on_both_lanes();
}
