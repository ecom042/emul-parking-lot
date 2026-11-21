/*
 * Tests of fsm/lanes: events_post() puts the event in the queue of the lane
 * it names, and only there.
 */
#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "fsm/lanes.h"
#include "services/events.h"

static struct k_msgq *queue_of(enum lane_id id)
{
	return lanes_get(id)->queue;
}

static void empty_queues(void *fixture)
{
	ARG_UNUSED(fixture);
	k_msgq_purge(queue_of(LANE_ENTRY));
	k_msgq_purge(queue_of(LANE_EXIT));
}

ZTEST_SUITE(fsm_lanes, NULL, NULL, empty_queues, NULL, NULL);

ZTEST(fsm_lanes, test_each_lane_knows_its_id)
{
	zassert_equal(lanes_get(LANE_ENTRY)->id, LANE_ENTRY);
	zassert_equal(lanes_get(LANE_EXIT)->id, LANE_EXIT);
}

ZTEST(fsm_lanes, test_event_goes_to_the_named_lane_only)
{
	uint8_t event;

	zassert_ok(events_post(LANE_ENTRY, EV_UPPER_REACHED));
	zassert_equal(k_msgq_num_used_get(queue_of(LANE_EXIT)), 0, "exit untouched");
	zassert_ok(k_msgq_get(queue_of(LANE_ENTRY), &event, K_NO_WAIT));
	zassert_equal(event, EV_UPPER_REACHED);
}

ZTEST(fsm_lanes, test_post_to_full_lane_queue_fails)
{
	for (int i = 0; i < LANES_QUEUE_LEN; i++) {
		zassert_ok(events_post(LANE_EXIT, EV_BOOT));
	}
	zassert_equal(events_post(LANE_EXIT, EV_BOOT), -ENOMSG);
}
