/**
 * @file test_distance.c
 * @brief Testes do dono do ADC4 (distance.c), com o ADC falso.
 */

#include <errno.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "distance.h"

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, hw_adc_start, enum hw_lane);
FAKE_VALUE_FUNC(int, hw_adc_poll, uint16_t *);

static uint16_t next_raw;

/** @brief Conversão concluída com next_raw. */
static int poll_done(uint16_t *raw)
{
	*raw = next_raw;
	return 0;
}

static void reset_fakes(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(hw_adc_start);
	RESET_FAKE(hw_adc_poll);
	hw_adc_poll_fake.custom_fake = poll_done;
	next_raw = 0;
	distance_init();
}

ZTEST_SUITE(distance, NULL, NULL, reset_fakes, NULL, NULL);

ZTEST(distance, test_alternates_lanes_one_conversion_at_a_time)
{
	uint16_t raw;

	distance_service(0);
	zassert_equal(hw_adc_start_fake.call_count, 1);
	zassert_equal(hw_adc_start_fake.arg0_history[0], HW_LANE_ENTRY);
	zassert_equal(hw_adc_poll_fake.call_count, 0);

	next_raw = 500;
	distance_service(1);
	zassert_equal(hw_adc_start_fake.arg0_history[1], HW_LANE_EXIT);
	zassert_ok(distance_get(HW_LANE_ENTRY, 1, &raw));
	zassert_equal(raw, 500);
	zassert_equal(distance_get(HW_LANE_EXIT, 1, &raw), -ENODATA);

	next_raw = 3000;
	distance_service(2);
	zassert_equal(hw_adc_start_fake.arg0_history[2], HW_LANE_ENTRY);
	zassert_ok(distance_get(HW_LANE_EXIT, 2, &raw));
	zassert_equal(raw, 3000);
	zassert_false(distance_faulted());
}

ZTEST(distance, test_pending_conversion_is_not_resubmitted)
{
	distance_service(0);
	hw_adc_poll_fake.custom_fake = NULL;
	hw_adc_poll_fake.return_val = -EAGAIN;
	distance_service(50);
	distance_service(DISTANCE_DEADLINE_MS - 1);
	zassert_equal(hw_adc_start_fake.call_count, 1);
	zassert_false(distance_faulted());
}

ZTEST(distance, test_old_sample_is_not_fresh)
{
	uint16_t raw;

	distance_service(0);
	distance_service(1);
	zassert_ok(distance_get(HW_LANE_ENTRY, DISTANCE_FRESH_MS, &raw));
	zassert_equal(distance_get(HW_LANE_ENTRY, 1 + DISTANCE_FRESH_MS, &raw), -ENODATA);
}

ZTEST(distance, test_missed_deadline_latches_fault)
{
	uint16_t raw;

	distance_service(0);
	hw_adc_poll_fake.custom_fake = NULL;
	hw_adc_poll_fake.return_val = -EAGAIN;
	distance_service(DISTANCE_DEADLINE_MS);
	zassert_true(distance_faulted());
	zassert_equal(distance_get(HW_LANE_ENTRY, DISTANCE_DEADLINE_MS, &raw), -EIO);

	hw_adc_poll_fake.custom_fake = poll_done;
	distance_service(DISTANCE_DEADLINE_MS + 1);
	zassert_equal(hw_adc_start_fake.call_count, 1);
	zassert_true(distance_faulted());
}

ZTEST(distance, test_late_completion_latches_fault)
{
	distance_service(0);
	distance_service(DISTANCE_DEADLINE_MS);
	zassert_true(distance_faulted());
}

ZTEST(distance, test_conversion_error_latches_fault)
{
	distance_service(0);
	hw_adc_poll_fake.custom_fake = NULL;
	hw_adc_poll_fake.return_val = -EIO;
	distance_service(1);
	zassert_true(distance_faulted());
}

ZTEST(distance, test_invalid_sample_latches_fault)
{
	distance_service(0);
	next_raw = DISTANCE_MAX_CODE + 1;
	distance_service(1);
	zassert_true(distance_faulted());
}

ZTEST(distance, test_submission_error_latches_fault)
{
	hw_adc_start_fake.return_val = -EBUSY;
	distance_service(0);
	zassert_true(distance_faulted());
}
