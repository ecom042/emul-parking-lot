/*
 * Tests of services/presence. Distance samples are published on the real
 * chan_distance (from services/distance.c), whose zbus listener runs at once
 * in the publisher's context, so each publish is fully handled on return.
 * The beam is hw/sensor, faked by FFF: the test keeps the handler presence
 * registers and plays edges with it. events_post() is the real one
 * (fsm/lanes.c): the tests read the entry lane's queue.
 *
 * Only the entry lane is initialized, so exit samples must be ignored. Once
 * these tests ran, entry samples published by the distance tests also post
 * EV_CAR_* events; those tests only count EV_ADC_FAULT, so it does not matter.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/ztest.h>

#include "fsm/lanes.h"
#include "hw/lane_hw.h"
#include "hw/sensor.h"
#include "services/distance.h"
#include "services/events.h"
#include "services/presence.h"

/* Defined in barrier/test_barrier.c: a fake exists once per binary. */
DECLARE_FAKE_VALUE_FUNC(int, sensor_init, struct sensor *, const struct gpio_dt_spec *,
			sensor_handler_fn, void *);
DECLARE_FAKE_VALUE_FUNC(bool, sensor_is_active, const struct sensor *);

/* Codes around the thresholds (683 arrived, 1024 left). */
#define CODE_AT_800_MM     546
#define CODE_IN_BAND       850
#define CODE_AT_6000_MM    4095
#define ARRIVED_LIMIT_CODE 683
#define LEFT_LIMIT_CODE    1024

/* Largest noise a scenario adds (REQ-03): +-64 codes. */
#define NOISE_CODES 64

static sensor_handler_fn beam_handler;
static void *beam_ctx;
static const struct gpio_dt_spec *beam_pin;

static int capture_beam_handler(struct sensor *sensor, const struct gpio_dt_spec *pin,
				sensor_handler_fn handler, void *ctx)
{
	ARG_UNUSED(sensor);
	beam_pin = pin;
	beam_handler = handler;
	beam_ctx = ctx;
	return 0;
}

static void publish(enum lane_id lane, uint16_t code)
{
	struct distance_sample sample = {.lane = lane, .code = code, .taken_ms = k_uptime_get()};

	zassert_ok(zbus_chan_pub(&chan_distance, &sample, K_NO_WAIT));
}

static struct k_msgq *queue_of(enum lane_id lane)
{
	return lanes_get(lane)->queue;
}

static void assert_events(enum lane_id lane, const uint8_t *expected, size_t count)
{
	uint8_t event;

	for (size_t i = 0; i < count; i++) {
		zassert_ok(k_msgq_get(queue_of(lane), &event, K_NO_WAIT), "event %u missing", i);
		zassert_equal(event, expected[i], "event %u", i);
	}
	zassert_equal(k_msgq_num_used_get(queue_of(lane)), 0, "unexpected extra event");
}

static void assert_no_event(enum lane_id lane)
{
	assert_events(lane, NULL, 0);
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(sensor_init);
	RESET_FAKE(sensor_is_active);
	sensor_init_fake.custom_fake = capture_beam_handler;
	zassert_ok(presence_init(LANE_ENTRY));
	k_msgq_purge(queue_of(LANE_ENTRY));
	k_msgq_purge(queue_of(LANE_EXIT));
}

ZTEST_SUITE(presence, NULL, NULL, reset, NULL, NULL);

ZTEST(presence, test_init_watches_the_lane_beam)
{
	zassert_equal(sensor_init_fake.call_count, 1);
	zassert_equal(beam_pin, &lane_hw_get(LANE_ENTRY)->beam);
	zassert_not_null(beam_handler);
}

ZTEST(presence, test_init_reports_a_beam_setup_error)
{
	sensor_init_fake.custom_fake = NULL;
	sensor_init_fake.return_val = -ENODEV;

	zassert_equal(presence_init(LANE_ENTRY), -ENODEV);
}

ZTEST(presence, test_before_any_sample_the_car_is_neither_near_nor_far)
{
	zassert_false(presence_car_near(LANE_ENTRY));
	zassert_false(presence_car_far(LANE_ENTRY));
}

ZTEST(presence, test_first_sample_below_683_is_an_arrival)
{
	publish(LANE_ENTRY, CODE_AT_800_MM);

	zassert_true(presence_car_near(LANE_ENTRY));
	zassert_false(presence_car_far(LANE_ENTRY));
	assert_events(LANE_ENTRY, (const uint8_t[]){EV_CAR_ARRIVED}, 1);
}

ZTEST(presence, test_first_sample_above_1024_means_far)
{
	publish(LANE_ENTRY, CODE_AT_6000_MM);

	zassert_true(presence_car_far(LANE_ENTRY));
	assert_events(LANE_ENTRY, (const uint8_t[]){EV_CAR_LEFT}, 1);
}

ZTEST(presence, test_sample_in_band_keeps_presence_unknown)
{
	publish(LANE_ENTRY, CODE_IN_BAND);

	zassert_false(presence_car_near(LANE_ENTRY));
	zassert_false(presence_car_far(LANE_ENTRY));
	assert_no_event(LANE_ENTRY);
}

ZTEST(presence, test_thresholds_are_strict)
{
	publish(LANE_ENTRY, ARRIVED_LIMIT_CODE);
	publish(LANE_ENTRY, LEFT_LIMIT_CODE);

	zassert_false(presence_car_near(LANE_ENTRY), "683 is not near");
	zassert_false(presence_car_far(LANE_ENTRY), "1024 is not far");
	assert_no_event(LANE_ENTRY);
}

ZTEST(presence, test_full_approach_and_departure)
{
	publish(LANE_ENTRY, CODE_AT_6000_MM);
	publish(LANE_ENTRY, CODE_IN_BAND);
	publish(LANE_ENTRY, CODE_AT_800_MM);
	publish(LANE_ENTRY, CODE_IN_BAND);
	publish(LANE_ENTRY, LEFT_LIMIT_CODE + 1);

	zassert_true(presence_car_far(LANE_ENTRY));
	assert_events(LANE_ENTRY, (const uint8_t[]){EV_CAR_LEFT, EV_CAR_ARRIVED, EV_CAR_LEFT},
		      3);
}

/* REQ-03, scenario noisy-threshold: a car parked at 1000 mm (code 682). */
ZTEST(presence, test_noise_near_683_is_one_arrival)
{
	const uint16_t parked_code = ARRIVED_LIMIT_CODE - 1;
	const int16_t noise[] = {0, NOISE_CODES, -NOISE_CODES, 1, -1, NOISE_CODES, -40};

	for (size_t i = 0; i < ARRAY_SIZE(noise); i++) {
		publish(LANE_ENTRY, parked_code + noise[i]);
	}

	zassert_true(presence_car_near(LANE_ENTRY));
	assert_events(LANE_ENTRY, (const uint8_t[]){EV_CAR_ARRIVED}, 1);
}

ZTEST(presence, test_stationary_car_is_one_arrival)
{
	for (int i = 0; i < 10; i++) {
		publish(LANE_ENTRY, CODE_AT_800_MM);
	}

	assert_events(LANE_ENTRY, (const uint8_t[]){EV_CAR_ARRIVED}, 1);
}

ZTEST(presence, test_samples_of_an_unwatched_lane_are_ignored)
{
	publish(LANE_EXIT, CODE_AT_800_MM);

	zassert_false(presence_car_near(LANE_EXIT));
	zassert_false(presence_car_near(LANE_ENTRY), "entry untouched");
	assert_no_event(LANE_EXIT);
	assert_no_event(LANE_ENTRY);
}

ZTEST(presence, test_init_resets_presence_to_unknown)
{
	publish(LANE_ENTRY, CODE_AT_800_MM);

	zassert_ok(presence_init(LANE_ENTRY));

	zassert_false(presence_car_near(LANE_ENTRY));
}

ZTEST(presence, test_beam_edges_post_on_and_off)
{
	beam_handler(beam_ctx, true);
	beam_handler(beam_ctx, false);

	assert_events(LANE_ENTRY, (const uint8_t[]){EV_BEAM_ON, EV_BEAM_OFF}, 2);
}

ZTEST(presence, test_beam_active_reads_the_pin)
{
	sensor_is_active_fake.return_val = true;
	zassert_true(presence_beam_active(LANE_ENTRY));

	sensor_is_active_fake.return_val = false;
	zassert_false(presence_beam_active(LANE_ENTRY));
}
