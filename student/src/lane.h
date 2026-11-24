#ifndef PARKING_LANE_H
#define PARKING_LANE_H

#include <stdbool.h>

#include "parking.h"

enum parking_state {
	PARKING_STARTUP,
	PARKING_IDLE,
	PARKING_IDENTIFYING,
	PARKING_AUTHORIZING,
	PARKING_OPENING,
	PARKING_PASSAGE,
	PARKING_WAIT_CLEAR,
	PARKING_CLOSING,
	PARKING_ASSISTANCE,
};

enum parking_motor { PARKING_STOP, PARKING_OPEN, PARKING_CLOSE };

struct parking_io {
	/* CLOSE may return -EAGAIN if the beam became active before enable. */
	int (*motor)(enum parking_lane_id lane, enum parking_motor command);
	int (*signal)(enum parking_lane_id lane, bool green);
	int (*buzzer)(enum parking_lane_id lane, bool on);
};

struct parking_distance {
	uint16_t raw;
	int64_t sampled_ms;
	bool valid;
};

struct parking_inputs {
	bool upper;
	bool lower;
	bool beam;
	bool beam_rose;
	bool gpio_fault;
	bool adc_fault;
	struct parking_distance distance;
};

struct parking_lane {
	enum parking_lane_id id;
	enum parking_state state;
	const struct parking_io *io;
	int64_t deadline_ms;
	int64_t buzzer_off_ms;
	uint32_t serial;
	uint32_t transaction;
	unsigned int attempt;
	struct parking_reply reply;
	bool reply_pending;
	bool beam_seen;
	bool latched;
	bool buzz_on;
	bool safe_outputs_pending;
};

/* Hardware must already be configured with safe outputs. */
void parking_lane_init(struct parking_lane *lane, enum parking_lane_id id,
		       const struct parking_io *io, int64_t ready_ms);
void parking_lane_receive(struct parking_lane *lane, const struct parking_reply *reply);
void parking_lane_step(struct parking_lane *lane, const struct parking_inputs *inputs,
		       const struct parking_services *services, int64_t now_ms);

#endif
