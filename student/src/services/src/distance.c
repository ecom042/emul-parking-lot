#include "services/distance.h"

#include <errno.h>

#include <zephyr/kernel.h>

#include "hw/adc4.h"
#include "services/events.h"

#define DISTANCE_DEADLINE_MS 100 /**< a conversion must be observed done within this */
#define DISTANCE_POLL_MS     5   /**< period of the poll work item */

ZBUS_CHAN_DEFINE(chan_distance, struct distance_sample, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

enum dist_state {
	DIST_IDLE,    /**< no conversion running: the next poll starts one */
	DIST_PENDING, /**< conversion started, not yet observed complete */
	DIST_LATCHED, /**< fault (or failed init): nothing runs until reset */
};

/*
 * Acquisition state. Only one context touches it (the work item, or the test
 * calling distance_poll()), so there is no mutex.
 */
static struct {
	enum dist_state state;
	enum lane_id lane;  /**< lane of the conversion running or next to run */
	int64_t started_ms; /**< virtual time the current conversion started */
} acq;

static void poll_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(poll_work, poll_handler);

/* Latch first, so the state is consistent before any event is posted. */
static void fault(void)
{
	acq.state = DIST_LATCHED;
	(void)events_post(LANE_ENTRY, EV_ADC_FAULT);
	(void)events_post(LANE_EXIT, EV_ADC_FAULT);
}

static void start(int64_t now_ms)
{
	acq.started_ms = now_ms;
	acq.state = DIST_PENDING;
	if (adc4_start(lane_hw_get(acq.lane)->distance_cfg.channel_id) != 0) {
		fault();
	}
}

static void handle_pending(int64_t now_ms)
{
	struct distance_sample sample;
	uint16_t code;
	int rc = adc4_poll(&code);

	if (rc == -EAGAIN) {
		if (now_ms - acq.started_ms >= DISTANCE_DEADLINE_MS) {
			fault();
		}
		return;
	}
	/* Done at or after the deadline counts as a failure, not a sample. */
	if (rc != 0 || now_ms - acq.started_ms >= DISTANCE_DEADLINE_MS) {
		fault();
		return;
	}
	sample.lane = acq.lane;
	sample.code = code;
	sample.taken_ms = now_ms;
	/* A lost sample is not an ADC fault: the next one replaces it. */
	(void)zbus_chan_pub(&chan_distance, &sample, K_NO_WAIT);
	acq.lane = (acq.lane == LANE_ENTRY) ? LANE_EXIT : LANE_ENTRY;
	start(now_ms);
}

void distance_poll(int64_t now_ms)
{
	switch (acq.state) {
	case DIST_IDLE:
		start(now_ms);
		break;
	case DIST_PENDING:
		handle_pending(now_ms);
		break;
	case DIST_LATCHED:
	default:
		break;
	}
}

static void poll_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	distance_poll(k_uptime_get());
	if (acq.state != DIST_LATCHED) {
		(void)k_work_schedule(&poll_work, K_MSEC(DISTANCE_POLL_MS));
	}
}

int distance_init(void)
{
	struct adc_channel_cfg cfgs[LANE_COUNT];
	struct k_work_sync sync;
	int rc;

	/* Stop a poll left over from an earlier init before reconfiguring. */
	(void)k_work_cancel_delayable_sync(&poll_work, &sync);
	for (int i = 0; i < LANE_COUNT; i++) {
		cfgs[i] = lane_hw_get((enum lane_id)i)->distance_cfg;
	}
	rc = adc4_init(distance_adc_get(), cfgs, LANE_COUNT);
	if (rc != 0) {
		acq.state = DIST_LATCHED;
		return rc;
	}
	acq.state = DIST_IDLE;
	acq.lane = LANE_ENTRY;
	return 0;
}

void distance_start(void)
{
	(void)k_work_schedule(&poll_work, K_NO_WAIT);
}
