/*
 * Tests of services/tariff. Written in units of CONFIG_PARKING_TARIFF_UNIT_MS,
 * so they hold for whatever unit the suite is built with.
 */
#include <errno.h>
#include <stdint.h>

#include <zephyr/ztest.h>

#include "services/tariff.h"

#define UNIT_MS     ((int64_t)CONFIG_PARKING_TARIFF_UNIT_MS)
#define UNSET_CENTS 12345U

static uint32_t cents_for(int64_t parked_ms)
{
	uint32_t cents = UNSET_CENTS;

	zassert_ok(tariff_cents(parked_ms, &cents), "parked %lld ms", parked_ms);
	return cents;
}

ZTEST_SUITE(tariff, NULL, NULL, NULL, NULL, NULL);

ZTEST(tariff, test_no_time_costs_the_minimum)
{
	zassert_equal(cents_for(0), 200);
}

ZTEST(tariff, test_the_first_unit_costs_200)
{
	zassert_equal(cents_for(1), 200, "a started unit counts");
	zassert_equal(cents_for(UNIT_MS), 200, "exactly one unit");
}

ZTEST(tariff, test_each_started_unit_adds_100)
{
	zassert_equal(cents_for(UNIT_MS + 1), 300);
	zassert_equal(cents_for(2 * UNIT_MS), 300);
}

/* Specification example: 2.5 s with a 1000 ms unit is 3 units, BRL 4.00. */
ZTEST(tariff, test_two_and_a_half_units_cost_400)
{
	zassert_equal(cents_for(UNIT_MS * 5 / 2), 400);
}

ZTEST(tariff, test_the_largest_stay_costs_the_terminal_maximum)
{
	zassert_equal(cents_for(TARIFF_MAX_UNITS * UNIT_MS), 1000000);
}

ZTEST(tariff, test_beyond_the_largest_stay_is_out_of_range)
{
	uint32_t cents = UNSET_CENTS;

	zassert_equal(tariff_cents(TARIFF_MAX_UNITS * UNIT_MS + 1, &cents), -ERANGE);
	zassert_equal(cents, UNSET_CENTS, "untouched on error");
}

ZTEST(tariff, test_a_huge_stay_does_not_overflow)
{
	uint32_t cents;

	zassert_equal(tariff_cents(INT64_MAX, &cents), -ERANGE);
}
