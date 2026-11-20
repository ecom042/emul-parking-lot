/*
 * Tests of services/barrier with FFF fakes in place of hw/motor and hw/sensor.
 *
 * The sensor_init fake records the handler the barrier registers, so a test
 * can play a limit switch edge: set the levels, call the handler (as the ISR
 * would) and wait for the work item. events_post() is the real one from
 * fsm/lanes.c: the tests read the events from the entry lane's queue.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "fsm/lanes.h"
#include "hw/lane_hw.h"
#include "hw/motor.h"
#include "hw/sensor.h"
#include "services/barrier.h"
#include "services/events.h"

/* DEFINE_FFF_GLOBALS lives in test_exemplo.c: one per test binary. */
FAKE_VALUE_FUNC(int, motor_init, const struct motor *);
FAKE_VALUE_FUNC(int, motor_drive, const struct motor *, enum motor_direction);
FAKE_VALUE_FUNC(int, motor_stop, const struct motor *);
FAKE_VALUE_FUNC(int, sensor_init, struct sensor *, const struct gpio_dt_spec *,
		sensor_handler_fn, void *);
FAKE_VALUE_FUNC(bool, sensor_is_active, const struct sensor *);

/* Long enough for the system work queue to run the limit work item. */
#define WORK_SETTLE_MS 5

static const struct lane_hw *entry;

/* What the barrier registered, and the levels the fake sensors report. */
static const struct sensor *upper_sensor;
static const struct sensor *lower_sensor;
static sensor_handler_fn limit_handler;
static void *limit_ctx;
static bool upper_level;
static bool lower_level;
static int sensor_init_result;

static int fake_sensor_init(struct sensor *sensor, const struct gpio_dt_spec *pin,
			    sensor_handler_fn handler, void *ctx)
{
	if (pin == &entry->limit_upper) {
		upper_sensor = sensor;
	} else {
		lower_sensor = sensor;
	}
	limit_handler = handler;
	limit_ctx = ctx;
	return sensor_init_result;
}

static bool fake_sensor_is_active(const struct sensor *sensor)
{
	return (sensor == upper_sensor) ? upper_level : lower_level;
}

/* Play an edge: new levels, then the handler as the ISR would call it. */
static void set_limits(bool upper, bool lower)
{
	upper_level = upper;
	lower_level = lower;
	limit_handler(limit_ctx, true);
	k_msleep(WORK_SETTLE_MS);
}

static void assert_one_event(enum lane_event expected)
{
	uint8_t event;
	struct k_msgq *queue = lanes_get(LANE_ENTRY)->queue;

	zassert_ok(k_msgq_get(queue, &event, K_NO_WAIT), "no event posted");
	zassert_equal(event, expected);
	zassert_equal(k_msgq_num_used_get(queue), 0, "only one event");
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(motor_init);
	RESET_FAKE(motor_drive);
	RESET_FAKE(motor_stop);
	RESET_FAKE(sensor_init);
	RESET_FAKE(sensor_is_active);
	FFF_RESET_HISTORY();
	sensor_init_fake.custom_fake = fake_sensor_init;
	sensor_is_active_fake.custom_fake = fake_sensor_is_active;
	sensor_init_result = 0;
	upper_level = false;
	lower_level = false;
	entry = lane_hw_get(LANE_ENTRY);
	k_msgq_purge(lanes_get(LANE_ENTRY)->queue);
	zassert_ok(barrier_init(LANE_ENTRY));
}

ZTEST_SUITE(barrier, NULL, NULL, reset, NULL, NULL);

ZTEST(barrier, test_init_sets_up_motor_and_both_limits)
{
	zassert_equal(motor_init_fake.call_count, 1);
	zassert_equal_ptr(motor_init_fake.arg0_val, &entry->motor);
	zassert_equal(sensor_init_fake.call_count, 2);
	zassert_equal_ptr(sensor_init_fake.arg1_history[0], &entry->limit_upper);
	zassert_equal_ptr(sensor_init_fake.arg1_history[1], &entry->limit_lower);
}

ZTEST(barrier, test_init_reports_motor_error_before_sensors)
{
	RESET_FAKE(sensor_init);
	motor_init_fake.return_val = -EIO;
	zassert_equal(barrier_init(LANE_ENTRY), -EIO);
	zassert_equal(sensor_init_fake.call_count, 0);
}

ZTEST(barrier, test_init_reports_upper_limit_error)
{
	sensor_init_result = -ENOTSUP;
	sensor_init_fake.call_count = 0;
	zassert_equal(barrier_init(LANE_ENTRY), -ENOTSUP);
	zassert_equal(sensor_init_fake.call_count, 1, "lower not configured");
}

ZTEST(barrier, test_open_close_stop_drive_the_motor)
{
	zassert_ok(barrier_open(LANE_ENTRY));
	zassert_equal(motor_drive_fake.arg1_history[0], MOTOR_OPEN);
	zassert_ok(barrier_close(LANE_ENTRY));
	zassert_equal(motor_drive_fake.arg1_history[1], MOTOR_CLOSE);
	zassert_ok(barrier_stop(LANE_ENTRY));
	zassert_equal(motor_stop_fake.call_count, 1);
	zassert_equal_ptr(motor_stop_fake.arg0_val, &entry->motor);
}

ZTEST(barrier, test_upper_limit_stops_motor_then_posts_event)
{
	set_limits(true, false);
	zassert_equal(motor_stop_fake.call_count, 1);
	assert_one_event(EV_UPPER_REACHED);
}

ZTEST(barrier, test_lower_limit_stops_motor_then_posts_event)
{
	set_limits(false, true);
	zassert_equal(motor_stop_fake.call_count, 1);
	assert_one_event(EV_LOWER_REACHED);
}

ZTEST(barrier, test_both_limits_stop_motor_and_report_conflict)
{
	set_limits(true, true);
	zassert_equal(motor_stop_fake.call_count, 1);
	assert_one_event(EV_LIMITS_CONFLICT);
}

ZTEST(barrier, test_leaving_a_limit_does_nothing)
{
	set_limits(false, false);
	zassert_equal(motor_stop_fake.call_count, 0);
	zassert_equal(k_msgq_num_used_get(lanes_get(LANE_ENTRY)->queue), 0);
}

ZTEST(barrier, test_is_closed_only_with_lower_alone)
{
	lower_level = true;
	upper_level = false;
	zassert_true(barrier_is_closed(LANE_ENTRY));

	upper_level = true;
	zassert_false(barrier_is_closed(LANE_ENTRY), "contradictory is not closed");

	lower_level = false;
	zassert_false(barrier_is_closed(LANE_ENTRY));

	upper_level = false;
	zassert_false(barrier_is_closed(LANE_ENTRY));
}
