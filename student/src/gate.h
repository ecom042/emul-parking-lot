#ifndef PARKING_GATE_H
#define PARKING_GATE_H

#include <stdbool.h>
#include <stdint.h>

#define GATE_MOTION_MS 2500
#define GATE_BUZZER_MS 100

enum gate_state {
	GATE_CLOSED,
	GATE_OPENING,
	GATE_OPEN,
	GATE_CLOSING,
	GATE_ASSISTANCE,
};

struct gate_inputs {
	bool upper;
	bool lower;
	bool beam;
};

enum gate_output {
	GATE_ENABLE,
	GATE_DIRECTION,
	GATE_RED,
	GATE_GREEN,
	GATE_BUZZER,
};

struct gate {
	enum gate_state state;
	int64_t deadline;
	bool latched;
	bool buzzing;
	int (*write)(void *context, enum gate_output output, bool active);
	void *context;
};

/* Call the state-machine functions from one serialized control context. */
void gate_assist(struct gate *gate, int64_t now, bool latch);
void gate_step(struct gate *gate, struct gate_inputs input, int64_t now);
bool gate_open(struct gate *gate, struct gate_inputs input, int64_t now);
bool gate_close(struct gate *gate, struct gate_inputs input, int64_t now);
bool gate_rearm(struct gate *gate, struct gate_inputs input, bool vehicle_gone);

#endif
