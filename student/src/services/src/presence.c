#include "services/presence.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/zbus/zbus.h>

#include "hw/sensor.h"
#include "services/distance.h"
#include "services/events.h"

/* REQ-02/03 thresholds in raw ADC codes: about 1000 mm and 1500 mm. */
#define ARRIVED_BELOW_CODE 683
#define LEFT_ABOVE_CODE    1024

/* zbus calls observers in priority order; presence is the only firmware one. */
#define PRESENCE_OBSERVER_PRIORITY 1

enum car_presence {
	CAR_UNKNOWN, /**< no fresh sample has crossed a threshold yet */
	CAR_NEAR,    /**< last crossing: below ARRIVED_BELOW_CODE */
	CAR_FAR,     /**< last crossing: above LEFT_ABOVE_CODE */
};

/*
 * Per-lane state. `car` is written by the distance work item (through the
 * zbus listener) and read by the lane thread, hence atomic.
 */
static struct lane_presence {
	struct sensor beam; /**< passage/obstruction beam */
	atomic_t car;       /**< enum car_presence */
	atomic_t watching;  /**< non-zero after presence_init(): samples count */
	enum lane_id id;    /**< lane this entry serves */
} self[LANE_COUNT];

/* Where a fresh code moves the presence; the band between keeps it. */
static enum car_presence presence_after(enum car_presence now, uint16_t code)
{
	if (code < ARRIVED_BELOW_CODE) {
		return CAR_NEAR;
	}
	if (code > LEFT_ABOVE_CODE) {
		return CAR_FAR;
	}
	return now;
}

/* Update first, post after: the lane reading presence_car_far() on
 * EV_CAR_LEFT already sees the new state.
 */
static void apply_sample(struct lane_presence *lane, uint16_t code)
{
	enum car_presence before = atomic_get(&lane->car);
	enum car_presence after = presence_after(before, code);

	if (after == before) {
		return;
	}
	atomic_set(&lane->car, after);
	(void)events_post(lane->id, (after == CAR_NEAR) ? EV_CAR_ARRIVED : EV_CAR_LEFT);
}

/* zbus listener: runs in the publisher's context (the distance work item),
 * so it only updates the state and posts.
 */
static void on_distance_sample(const struct zbus_channel *chan)
{
	const struct distance_sample *sample = zbus_chan_const_msg(chan);
	struct lane_presence *lane = &self[sample->lane];

	if (!atomic_get(&lane->watching)) {
		return;
	}
	apply_sample(lane, sample->code);
}

ZBUS_LISTENER_DEFINE(presence_listener, on_distance_sample);
ZBUS_CHAN_ADD_OBS(chan_distance, presence_listener, PRESENCE_OBSERVER_PRIORITY);

/* Beam handler, in interrupt context: posting does not block. */
static void on_beam_change(void *ctx, bool active)
{
	const struct lane_presence *lane = ctx;

	(void)events_post(lane->id, active ? EV_BEAM_ON : EV_BEAM_OFF);
}

int presence_init(enum lane_id id)
{
	struct lane_presence *lane = &self[id];

	lane->id = id;
	atomic_set(&lane->car, CAR_UNKNOWN);
	atomic_set(&lane->watching, true);
	return sensor_init(&lane->beam, &lane_hw_get(id)->beam, on_beam_change, lane);
}

bool presence_car_near(enum lane_id id)
{
	return atomic_get(&self[id].car) == CAR_NEAR;
}

bool presence_car_far(enum lane_id id)
{
	return atomic_get(&self[id].car) == CAR_FAR;
}

bool presence_beam_active(enum lane_id id)
{
	return sensor_is_active(&self[id].beam);
}
