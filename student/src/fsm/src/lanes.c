#include "fsm/lanes.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>

/* Static queues: they exist from boot, so an event posted early just waits. */
K_MSGQ_DEFINE(entry_queue, sizeof(uint8_t), LANES_QUEUE_LEN, 1);
K_MSGQ_DEFINE(exit_queue, sizeof(uint8_t), LANES_QUEUE_LEN, 1);

/* Rules and initial state are set when the lanes start (a later step). */
static struct lane lanes[LANE_COUNT] = {
	[LANE_ENTRY] = {.id = LANE_ENTRY, .queue = &entry_queue},
	[LANE_EXIT] = {.id = LANE_EXIT, .queue = &exit_queue},
};

struct lane *lanes_get(enum lane_id id)
{
	__ASSERT(id < LANE_COUNT, "invalid lane %d", id);
	return &lanes[id];
}

int events_post(enum lane_id lane, enum lane_event event)
{
	return lane_post(lanes_get(lane), event);
}
