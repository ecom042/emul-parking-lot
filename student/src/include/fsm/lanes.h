#ifndef PARKING_FSM_LANES_H_
#define PARKING_FSM_LANES_H_

/*
 * The two lanes of the parking lot: fsm/ owns them and their event queues,
 * indexed by lane id. This is what lets services publish an event knowing
 * only the lane id (events_post() in services/events.h is implemented here).
 */

#include "fsm/lane.h"
#include "services/events.h"

/*
 * Events waiting per lane. Each event is handled in well under a millisecond
 * and a lane sees only a few events per second, so 16 is a wide margin.
 */
#define LANES_QUEUE_LEN 16

/**
 * @brief The runtime of one lane.
 *
 * @param id LANE_ENTRY or LANE_EXIT; anything else is a programming error.
 * @return the lane, never NULL.
 */
struct lane *lanes_get(enum lane_id id);

#endif /* PARKING_FSM_LANES_H_ */
