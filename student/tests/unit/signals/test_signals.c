/*
 * Tests of services/signals with FFF fakes in place of hw/indicator.
 *
 * The fakes record which pin got which value, so each test checks the exact
 * outputs of a mode and their order. lane_hw.c is the real one: it is only
 * devicetree data.
 */
#include <errno.h>
#include <stdbool.h>

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "hw/indicator.h"
#include "hw/lane_hw.h"
#include "services/signals.h"

/* DEFINE_FFF_GLOBALS lives in test_exemplo.c: one per test binary. */
FAKE_VALUE_FUNC(int, indicator_init, const struct gpio_dt_spec *);
FAKE_VALUE_FUNC(int, indicator_set, const struct gpio_dt_spec *, bool);

/* Margins around the 100 ms pulse, in virtual time. */
#define BEFORE_PULSE_END_MS 50
#define AFTER_PULSE_END_MS 150

static const struct lane_hw *entry;

static void assert_set_call(unsigned int call, const struct gpio_dt_spec *pin, bool on)
{
	zassert_true(call < indicator_set_fake.call_count, "call %u missing", call);
	zassert_equal_ptr(indicator_set_fake.arg0_history[call], pin, "call %u: wrong pin", call);
	zassert_equal(indicator_set_fake.arg1_history[call], on, "call %u: wrong value", call);
}

static void reset_fakes(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(indicator_init);
	RESET_FAKE(indicator_set);
	FFF_RESET_HISTORY();
	entry = lane_hw_get(LANE_ENTRY);
	zassert_ok(signals_init(LANE_ENTRY));
	RESET_FAKE(indicator_init);
	RESET_FAKE(indicator_set);
}

/* Let a pending pulse end, so the next test's signals_init() never
 * re-initializes a work item that is still scheduled.
 */
static void wait_pulse_end(void *fixture)
{
	ARG_UNUSED(fixture);
	k_msleep(AFTER_PULSE_END_MS);
}

ZTEST_SUITE(signals, NULL, NULL, reset_fakes, wait_pulse_end, NULL);

ZTEST(signals, test_init_configures_three_pins_then_shows_stop)
{
	zassert_ok(signals_init(LANE_ENTRY));
	zassert_equal(indicator_init_fake.call_count, 3);
	zassert_equal_ptr(indicator_init_fake.arg0_history[0], &entry->red);
	zassert_equal_ptr(indicator_init_fake.arg0_history[1], &entry->green);
	zassert_equal_ptr(indicator_init_fake.arg0_history[2], &entry->buzzer);
	assert_set_call(0, &entry->green, false);
	assert_set_call(1, &entry->red, true);
}

ZTEST(signals, test_init_stops_at_first_pin_error)
{
	indicator_init_fake.return_val = -ENODEV;
	zassert_equal(signals_init(LANE_ENTRY), -ENODEV);
	zassert_equal(indicator_init_fake.call_count, 1);
	zassert_equal(indicator_set_fake.call_count, 0, "no output after a failed init");
}

ZTEST(signals, test_stop_turns_green_off_before_red_on)
{
	zassert_ok(signals_stop(LANE_ENTRY));
	zassert_equal(indicator_set_fake.call_count, 2);
	assert_set_call(0, &entry->green, false);
	assert_set_call(1, &entry->red, true);
}

ZTEST(signals, test_go_turns_red_off_before_green_on)
{
	zassert_ok(signals_go(LANE_ENTRY));
	zassert_equal(indicator_set_fake.call_count, 2);
	assert_set_call(0, &entry->red, false);
	assert_set_call(1, &entry->green, true);
}

ZTEST(signals, test_stop_reports_first_driver_error)
{
	indicator_set_fake.return_val = -EIO;
	zassert_equal(signals_stop(LANE_ENTRY), -EIO);
	zassert_equal(indicator_set_fake.call_count, 1, "red is not touched after the error");
}

ZTEST(signals, test_go_reports_first_driver_error)
{
	indicator_set_fake.return_val = -EIO;
	zassert_equal(signals_go(LANE_ENTRY), -EIO);
	zassert_equal(indicator_set_fake.call_count, 1, "green is not touched after the error");
}

ZTEST(signals, test_assist_shows_stop_and_starts_buzzer)
{
	zassert_ok(signals_assist(LANE_ENTRY));
	zassert_equal(indicator_set_fake.call_count, 3);
	assert_set_call(0, &entry->green, false);
	assert_set_call(1, &entry->red, true);
	assert_set_call(2, &entry->buzzer, true);
}

ZTEST(signals, test_assist_buzzer_turns_off_after_100_ms)
{
	zassert_ok(signals_assist(LANE_ENTRY));

	k_msleep(BEFORE_PULSE_END_MS);
	zassert_equal(indicator_set_fake.call_count, 3, "buzzer still on at 50 ms");

	k_msleep(AFTER_PULSE_END_MS - BEFORE_PULSE_END_MS);
	zassert_equal(indicator_set_fake.call_count, 4);
	assert_set_call(3, &entry->buzzer, false);
}

ZTEST(signals, test_assist_reports_stop_error_without_buzzer)
{
	indicator_set_fake.return_val = -EIO;
	zassert_equal(signals_assist(LANE_ENTRY), -EIO);
	zassert_equal(indicator_set_fake.call_count, 1);
}

ZTEST(signals, test_assist_reports_buzzer_error_without_pulse)
{
	static int results[] = {0, 0, -EIO};

	SET_RETURN_SEQ(indicator_set, results, ARRAY_SIZE(results));
	zassert_equal(signals_assist(LANE_ENTRY), -EIO);

	k_msleep(AFTER_PULSE_END_MS);
	zassert_equal(indicator_set_fake.call_count, 3, "no pulse end was scheduled");
}
