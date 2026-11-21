/*
 * Tests of the generic lane engine (src/fsm/src/lane.c) with a toy table.
 *
 * Toy rules:
 *   A --GO--> B                    (no guard)
 *   A --TRY--> C if guard, else B  (guard with otherwise)
 *   B --TRY--> A if guard          (guard without otherwise)
 *   B --GO--> C                    (C has no entry action)
 *   NOP does nothing anywhere.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "fsm/lane.h"

enum { ST_NONE = LANE_STATE_NONE, ST_A, ST_B, ST_C, ST_COUNT };
enum { EV_GO, EV_TRY, EV_NOP, EV_COUNT };

#define TOY_LANE_ID 7U

static bool guard_answer;
static int guard_calls;
static int enter_a_calls;
static int enter_b_calls;
static uint8_t last_entered_id;

static bool toy_guard(const struct lane *lane)
{
	ARG_UNUSED(lane);
	guard_calls++;
	return guard_answer;
}

static void enter_a(struct lane *lane)
{
	enter_a_calls++;
	last_entered_id = lane->id;
}

static void enter_b(struct lane *lane)
{
	enter_b_calls++;
	last_entered_id = lane->id;
}

static const struct lane_transition toy_table[ST_COUNT][EV_COUNT] = {
	[ST_A][EV_GO] = {.next = ST_B},
	[ST_A][EV_TRY] = {.next = ST_C, .guard = toy_guard, .otherwise = ST_B},
	[ST_B][EV_TRY] = {.next = ST_A, .guard = toy_guard},
	[ST_B][EV_GO] = {.next = ST_C},
};

static const lane_enter_fn toy_on_enter[ST_COUNT] = {
	[ST_A] = enter_a,
	[ST_B] = enter_b,
};

static const struct lane_def toy_def = {
	.table = &toy_table[0][0],
	.on_enter = toy_on_enter,
	.n_states = ST_COUNT,
	.n_events = EV_COUNT,
};

/* Small on purpose: two posts fill it, so the "full" case is easy to reach. */
#define TOY_QUEUE_LEN 2
K_MSGQ_DEFINE(toy_queue, sizeof(uint8_t), TOY_QUEUE_LEN, 1);

static struct lane toy;

static void reset_toy(void *fixture)
{
	ARG_UNUSED(fixture);
	guard_answer = false;
	guard_calls = 0;
	enter_a_calls = 0;
	enter_b_calls = 0;
	last_entered_id = 0;
	k_msgq_purge(&toy_queue);
	lane_init(&toy, TOY_LANE_ID, &toy_def, &toy_queue, ST_A);
}

ZTEST_SUITE(fsm_lane, NULL, NULL, reset_toy, NULL, NULL);

ZTEST(fsm_lane, test_init_sets_state_without_entry_action)
{
	zassert_equal(lane_state(&toy), ST_A);
	zassert_equal(toy.id, TOY_LANE_ID);
	zassert_equal(enter_a_calls, 0, "lane_init must not run entry actions");
}

ZTEST(fsm_lane, test_empty_cell_ignores_event)
{
	zassert_false(lane_dispatch(&toy, EV_NOP));
	zassert_equal(lane_state(&toy), ST_A);
	zassert_equal(enter_a_calls + enter_b_calls, 0);
}

ZTEST(fsm_lane, test_unguarded_transition_runs_entry_action_once)
{
	zassert_true(lane_dispatch(&toy, EV_GO));
	zassert_equal(lane_state(&toy), ST_B);
	zassert_equal(enter_b_calls, 1);
	zassert_equal(last_entered_id, TOY_LANE_ID, "entry action sees the lane");
}

ZTEST(fsm_lane, test_guard_yes_goes_to_next)
{
	guard_answer = true;
	zassert_true(lane_dispatch(&toy, EV_TRY));
	zassert_equal(guard_calls, 1);
	zassert_equal(lane_state(&toy), ST_C);
}

ZTEST(fsm_lane, test_guard_no_goes_to_otherwise)
{
	guard_answer = false;
	zassert_true(lane_dispatch(&toy, EV_TRY));
	zassert_equal(lane_state(&toy), ST_B);
	zassert_equal(enter_b_calls, 1);
}

ZTEST(fsm_lane, test_guard_no_without_otherwise_ignores_event)
{
	zassert_true(lane_dispatch(&toy, EV_GO)); /* A -> B */
	guard_answer = false;
	zassert_false(lane_dispatch(&toy, EV_TRY));
	zassert_equal(lane_state(&toy), ST_B);
	zassert_equal(enter_a_calls, 0);
}

ZTEST(fsm_lane, test_state_without_entry_action_is_entered)
{
	zassert_true(lane_dispatch(&toy, EV_GO)); /* A -> B */
	zassert_true(lane_dispatch(&toy, EV_GO)); /* B -> C, no action */
	zassert_equal(lane_state(&toy), ST_C);
}

ZTEST(fsm_lane, test_event_outside_table_is_ignored)
{
	zassert_false(lane_dispatch(&toy, EV_COUNT));
	zassert_equal(lane_state(&toy), ST_A);
}

ZTEST(fsm_lane, test_state_outside_table_ignores_events)
{
	lane_init(&toy, TOY_LANE_ID, &toy_def, &toy_queue, ST_COUNT);
	zassert_false(lane_dispatch(&toy, EV_GO));
	zassert_equal(lane_state(&toy), ST_COUNT);
}

ZTEST(fsm_lane, test_posted_event_is_applied_by_process)
{
	zassert_ok(lane_post(&toy, EV_GO));
	zassert_equal(lane_state(&toy), ST_A, "posting alone changes nothing");
	zassert_true(lane_process(&toy, K_NO_WAIT));
	zassert_equal(lane_state(&toy), ST_B);
	zassert_equal(enter_b_calls, 1);
}

ZTEST(fsm_lane, test_events_are_applied_in_posting_order)
{
	zassert_ok(lane_post(&toy, EV_GO)); /* A -> B */
	zassert_ok(lane_post(&toy, EV_GO)); /* B -> C */
	zassert_true(lane_process(&toy, K_NO_WAIT));
	zassert_equal(lane_state(&toy), ST_B);
	zassert_true(lane_process(&toy, K_NO_WAIT));
	zassert_equal(lane_state(&toy), ST_C);
}

ZTEST(fsm_lane, test_process_with_empty_queue_does_nothing)
{
	zassert_false(lane_process(&toy, K_NO_WAIT));
	zassert_equal(lane_state(&toy), ST_A);
}

ZTEST(fsm_lane, test_process_takes_event_even_if_ignored)
{
	zassert_ok(lane_post(&toy, EV_NOP));
	zassert_true(lane_process(&toy, K_NO_WAIT), "the event left the queue");
	zassert_equal(lane_state(&toy), ST_A);
	zassert_false(lane_process(&toy, K_NO_WAIT), "queue is now empty");
}

ZTEST(fsm_lane, test_post_to_full_queue_fails)
{
	for (int i = 0; i < TOY_QUEUE_LEN; i++) {
		zassert_ok(lane_post(&toy, EV_NOP));
	}
	zassert_equal(lane_post(&toy, EV_GO), -ENOMSG);
}
