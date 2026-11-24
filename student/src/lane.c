#include <errno.h>
#include <string.h>

#include "lane.h"

enum {
	NEAR_CODE = 683,
	CLEAR_CODE = 1024,
	MAX_DISTANCE_AGE_MS = 100,
	CAPTURE_ATTEMPTS = 3,
	CAPTURE_MS = 1000,
	ENTRY_AUTH_MS = 1000,
	EXIT_AUTH_MS = 10000,
	MOTION_MS = 2500,
	PASSAGE_MS = 10000,
	BUZZ_MS = 100,
};

static void cancel(struct parking_lane *lane, const struct parking_services *services)
{
	uint32_t transaction = lane->transaction;

	lane->transaction = 0;
	lane->reply_pending = false;
	if (transaction != 0 && services != NULL) {
		services->cancel(lane->id, transaction);
	}
}

static void safe_outputs(struct parking_lane *lane)
{
	int motor_rc = lane->io->motor(lane->id, PARKING_STOP);
	int signal_rc = lane->io->signal(lane->id, false);

	lane->safe_outputs_pending = motor_rc != 0 || signal_rc != 0;
	lane->latched |= lane->safe_outputs_pending;
}

static void assist(struct parking_lane *lane, const struct parking_services *services,
		   int64_t now_ms, bool latch)
{
	lane->latched |= latch;
	if (lane->state == PARKING_ASSISTANCE) {
		return;
	}

	lane->state = PARKING_ASSISTANCE;
	safe_outputs(lane);
	lane->buzz_on = true;
	lane->buzzer_off_ms = now_ms + BUZZ_MS;
	if (lane->io->buzzer(lane->id, true) != 0) {
		lane->latched = true;
	}
	cancel(lane, services);
}

static void capture(struct parking_lane *lane, const struct parking_services *services,
		    int64_t now_ms)
{
	if (lane->attempt == CAPTURE_ATTEMPTS || services == NULL) {
		assist(lane, services, now_ms, false);
		return;
	}

	lane->attempt++;
	lane->state = PARKING_IDENTIFYING;
	lane->deadline_ms = now_ms + CAPTURE_MS;
	const struct parking_request request = {
		.kind = PARKING_CAPTURE,
		.lane = lane->id,
		.transaction = lane->transaction,
		.attempt = lane->attempt,
		.deadline_ms = lane->deadline_ms,
	};
	if (services->request(&request) != 0) {
		assist(lane, services, now_ms, false);
	}
}

static void authorize(struct parking_lane *lane, const struct parking_services *services,
		      int64_t now_ms)
{
	bool entry = lane->id == PARKING_ENTRY;

	lane->state = PARKING_AUTHORIZING;
	lane->deadline_ms = now_ms + (entry ? ENTRY_AUTH_MS : EXIT_AUTH_MS);
	struct parking_request request = {
		.kind = entry ? PARKING_AUTHORIZE_ENTRY : PARKING_AUTHORIZE_EXIT,
		.lane = lane->id,
		.transaction = lane->transaction,
		.deadline_ms = lane->deadline_ms,
	};
	memcpy(request.plate, lane->reply.plate, sizeof(request.plate));
	if (services->request(&request) != 0) {
		assist(lane, services, now_ms, false);
	}
}

static void move(struct parking_lane *lane, const struct parking_services *services,
		 int64_t now_ms, enum parking_motor direction)
{
	int rc = lane->io->motor(lane->id, direction);

	if (direction == PARKING_CLOSE && rc == -EAGAIN) {
		return;
	}
	if (rc != 0) {
		assist(lane, services, now_ms, true);
		return;
	}
	lane->state = direction == PARKING_OPEN ? PARKING_OPENING : PARKING_CLOSING;
	lane->deadline_ms = now_ms + MOTION_MS;
}

static void handle_motion(struct parking_lane *lane, const struct parking_inputs *inputs,
			  const struct parking_services *services, int64_t now_ms)
{
	bool opening = lane->state == PARKING_OPENING;

	if (now_ms >= lane->deadline_ms || (!opening && (inputs->beam || inputs->beam_rose))) {
		assist(lane, services, now_ms, true);
		return;
	}
	if (!(opening ? inputs->upper : inputs->lower)) {
		return;
	}
	if (lane->io->motor(lane->id, PARKING_STOP) != 0) {
		assist(lane, services, now_ms, true);
		return;
	}
	if (!opening) {
		lane->state = PARKING_IDLE;
		return;
	}
	if (lane->io->signal(lane->id, true) != 0) {
		assist(lane, services, now_ms, true);
		return;
	}
	lane->state = PARKING_PASSAGE;
	lane->beam_seen = inputs->beam;
	lane->deadline_ms = now_ms + PASSAGE_MS;
}

static void handle_passage(struct parking_lane *lane, const struct parking_inputs *inputs,
			   const struct parking_services *services, int64_t now_ms, bool clear)
{
	if (!inputs->upper || now_ms >= lane->deadline_ms) {
		assist(lane, services, now_ms, true);
		return;
	}
	lane->beam_seen |= inputs->beam || inputs->beam_rose;
	if (!lane->beam_seen || inputs->beam || !clear) {
		return;
	}
	if (services->passage(lane->id, lane->transaction, now_ms) != 0) {
		assist(lane, services, now_ms, true);
		return;
	}
	lane->transaction = 0;
	if (lane->io->signal(lane->id, false) != 0) {
		assist(lane, services, now_ms, true);
		return;
	}
	lane->state = PARKING_WAIT_CLEAR;
	move(lane, services, now_ms, PARKING_CLOSE);
}

static void handle_request(struct parking_lane *lane, const struct parking_services *services,
			   int64_t now_ms, bool fresh, bool clear)
{
	bool identifying = lane->state == PARKING_IDENTIFYING;

	if (clear) {
		if (!identifying && lane->id == PARKING_EXIT) {
			assist(lane, services, now_ms, false);
			return;
		}
		cancel(lane, services);
		lane->state = PARKING_IDLE;
		return;
	}
	if (now_ms >= lane->deadline_ms) {
		lane->reply_pending = false;
		if (identifying) {
			capture(lane, services, now_ms);
		} else {
			assist(lane, services, now_ms, false);
		}
		return;
	}
	if (!lane->reply_pending || !fresh) {
		return;
	}
	lane->reply_pending = false;
	switch (lane->reply.kind) {
	case PARKING_NO_PLATE:
		capture(lane, services, now_ms);
		break;
	case PARKING_PLATE:
		authorize(lane, services, now_ms);
		break;
	case PARKING_DENIED:
		assist(lane, services, now_ms, false);
		break;
	case PARKING_ENTRY_ALLOWED:
	case PARKING_EXIT_PAID:
		move(lane, services, now_ms, PARKING_OPEN);
		break;
	}
}

void parking_lane_init(struct parking_lane *lane, enum parking_lane_id id,
		       const struct parking_io *io, int64_t ready_ms)
{
	*lane = (struct parking_lane){
		.id = id,
		.state = PARKING_STARTUP,
		.io = io,
		.deadline_ms = ready_ms,
	};
}

void parking_lane_receive(struct parking_lane *lane, const struct parking_reply *reply)
{
	if (lane->transaction == 0 || reply->transaction != lane->transaction ||
	    reply->lane != lane->id || lane->reply_pending) {
		return;
	}
	if (lane->state == PARKING_IDENTIFYING) {
		if (reply->attempt != lane->attempt || (reply->kind != PARKING_NO_PLATE &&
		    (reply->kind != PARKING_PLATE || reply->plate[PARKING_PLATE_SIZE - 1] != '\0'))) {
			return;
		}
	} else if (lane->state == PARKING_AUTHORIZING) {
		enum parking_reply_kind allowed = lane->id == PARKING_ENTRY ?
						 PARKING_ENTRY_ALLOWED : PARKING_EXIT_PAID;

		if (reply->attempt != 0 || (reply->kind != allowed && reply->kind != PARKING_DENIED)) {
			return;
		}
	} else {
		return;
	}
	lane->reply = *reply;
	lane->reply_pending = true;
}

void parking_lane_step(struct parking_lane *lane, const struct parking_inputs *inputs,
		       const struct parking_services *services, int64_t now_ms)
{
	bool closed = inputs->lower && !inputs->upper;
	bool fresh = inputs->distance.valid && inputs->distance.sampled_ms <= now_ms &&
		     now_ms - inputs->distance.sampled_ms < MAX_DISTANCE_AGE_MS;
	bool clear = fresh && inputs->distance.raw > CLEAR_CODE;

	if (inputs->gpio_fault || inputs->adc_fault || (inputs->upper && inputs->lower)) {
		assist(lane, services, now_ms, true);
	}

	switch (lane->state) {
	case PARKING_STARTUP:
		if (now_ms < lane->deadline_ms) {
			return;
		}
		lane->state = PARKING_IDLE;
		/* fall through */
	case PARKING_IDLE:
	case PARKING_IDENTIFYING:
	case PARKING_AUTHORIZING:
		if (!closed) {
			assist(lane, services, now_ms, true);
		} else if (lane->state != PARKING_IDLE) {
			handle_request(lane, services, now_ms, fresh, clear);
		} else if (fresh && inputs->distance.raw < NEAR_CODE && services != NULL) {
			if (++lane->serial == 0) {
				lane->serial++;
			}
			lane->transaction = lane->serial;
			lane->attempt = 0;
			capture(lane, services, now_ms);
		}
		break;
	case PARKING_OPENING:
	case PARKING_CLOSING:
		handle_motion(lane, inputs, services, now_ms);
		break;
	case PARKING_PASSAGE:
		handle_passage(lane, inputs, services, now_ms, clear);
		break;
	case PARKING_WAIT_CLEAR:
		if (!inputs->upper || now_ms >= lane->deadline_ms) {
			assist(lane, services, now_ms, true);
		} else if (!inputs->beam) {
			move(lane, services, now_ms, PARKING_CLOSE);
		}
		break;
	case PARKING_ASSISTANCE:
		if (lane->safe_outputs_pending) {
			safe_outputs(lane);
		}
		if (lane->buzz_on && now_ms >= lane->buzzer_off_ms) {
			lane->buzz_on = lane->io->buzzer(lane->id, false) != 0;
			lane->latched |= lane->buzz_on;
		}
		if (!lane->latched && !lane->buzz_on && closed && clear && !inputs->beam) {
			lane->state = PARKING_IDLE;
		}
		break;
	default:
		assist(lane, services, now_ms, true);
		break;
	}
}
