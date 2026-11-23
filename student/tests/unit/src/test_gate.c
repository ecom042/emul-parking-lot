#include <errno.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "gate.h"

FAKE_VALUE_FUNC(int, gate_write, void *, enum gate_output, bool);

static struct gate gate;
static const struct gate_inputs closed = { .lower = true };
static const struct gate_inputs opened = { .upper = true };
static const struct gate_inputs between = { 0 };

static void reset_gate(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(gate_write);
	FFF_RESET_HISTORY();
	gate = (struct gate) { .write = gate_write };
}

static void assert_write(unsigned int index, enum gate_output output, bool active)
{
	zassert_true(gate_write_fake.call_count > index);
	zassert_equal(gate_write_fake.arg1_history[index], output);
	zassert_equal(gate_write_fake.arg2_history[index], active);
}

static void open_gate(void)
{
	zassert_true(gate_open(&gate, closed, 0));
	gate_step(&gate, opened, 2000);
	zassert_equal(gate.state, GATE_OPEN);
}

ZTEST_SUITE(gate_safety, NULL, NULL, reset_gate, NULL, NULL);

ZTEST(gate_safety, test_limits_stop_motor_and_green_follows_stop)
{
	open_gate();
	assert_write(2, GATE_DIRECTION, true);
	assert_write(3, GATE_ENABLE, true);
	assert_write(4, GATE_ENABLE, false);
	assert_write(6, GATE_GREEN, true);

	zassert_true(gate_close(&gate, opened, 2100));
	assert_write(7, GATE_GREEN, false);
	assert_write(8, GATE_RED, true);
	assert_write(9, GATE_DIRECTION, false);
	gate_step(&gate, closed, 4200);
	assert_write(11, GATE_ENABLE, false);
	zassert_equal(gate.state, GATE_CLOSED);
}

ZTEST(gate_safety, test_startup_without_lower_does_not_home)
{
	gate_step(&gate, between, 0);
	zassert_equal(gate.state, GATE_ASSISTANCE);
	assert_write(0, GATE_ENABLE, false);
	assert_write(3, GATE_BUZZER, true);
	zassert_true(gate.latched);
}

ZTEST(gate_safety, test_contradictory_limits_latch)
{
	gate_step(&gate, (struct gate_inputs) { .upper = true, .lower = true }, 10);
	gate_step(&gate, closed, 109);
	zassert_true(gate.buzzing);
	gate_step(&gate, closed, 110);
	assert_write(4, GATE_BUZZER, false);
	zassert_false(gate_rearm(&gate, closed, true));
	zassert_false(gate_open(&gate, closed, 111));
}

ZTEST(gate_safety, test_opening_timeout_stops_motor)
{
	zassert_true(gate_open(&gate, closed, 0));
	gate_step(&gate, between, GATE_MOTION_MS - 1);
	zassert_equal(gate.state, GATE_OPENING);
	gate_step(&gate, between, GATE_MOTION_MS);
	assert_write(4, GATE_ENABLE, false);
	zassert_equal(gate.state, GATE_ASSISTANCE);
	zassert_true(gate.latched);
}

ZTEST(gate_safety, test_closing_timeout_stops_motor)
{
	open_gate();
	zassert_true(gate_close(&gate, opened, 2100));
	gate_step(&gate, between, 2100 + GATE_MOTION_MS);
	assert_write(11, GATE_ENABLE, false);
	zassert_equal(gate.state, GATE_ASSISTANCE);
}

ZTEST(gate_safety, test_beam_blocks_close_then_stops_moving_gate)
{
	struct gate_inputs blocked = { .upper = true, .beam = true };

	open_gate();
	zassert_false(gate_close(&gate, blocked, 2010));
	zassert_equal(gate_write_fake.call_count, 7);
	zassert_true(gate_close(&gate, opened, 2100));
	gate_step(&gate, blocked, 2110);
	assert_write(11, GATE_ENABLE, false);
	zassert_equal(gate.state, GATE_ASSISTANCE);
	zassert_false(gate_close(&gate, opened, 2300));
}

ZTEST(gate_safety, test_rearm_requires_gone_vehicle_and_clear_beam)
{
	struct gate_inputs blocked = { .lower = true, .beam = true };

	gate_assist(&gate, 0, false);
	gate_step(&gate, closed, 100);
	zassert_false(gate_rearm(&gate, closed, false));
	zassert_false(gate_rearm(&gate, blocked, true));
	zassert_true(gate_rearm(&gate, closed, true));
	zassert_equal(gate.state, GATE_CLOSED);
}

ZTEST(gate_safety, test_output_failure_requests_assistance)
{
	gate_write_fake.return_val = -EIO;
	zassert_false(gate_open(&gate, closed, 0));
	assert_write(1, GATE_ENABLE, false);
	assert_write(3, GATE_RED, true);
	zassert_equal(gate.state, GATE_ASSISTANCE);
	zassert_true(gate.latched);
}
