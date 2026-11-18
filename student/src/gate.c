#include "gate.h"

static bool set(struct gate *gate, enum gate_output output, bool active)
{
	return gate->write(gate->context, output, active) == 0;
}

void gate_assist(struct gate *gate, int64_t now, bool latch)
{
	/* Make every safe-output attempt even if an earlier write failed. */
	bool stopped = set(gate, GATE_ENABLE, false);
	bool green_off = set(gate, GATE_GREEN, false);
	bool red_on = set(gate, GATE_RED, true);

	gate->latched |= latch || !stopped || !green_off || !red_on;
	if (gate->state != GATE_ASSISTANCE) {
		gate->state = GATE_ASSISTANCE;
		gate->buzzing = set(gate, GATE_BUZZER, true);
		gate->deadline = now + GATE_BUZZER_MS;
	}
}

void gate_step(struct gate *gate, struct gate_inputs input, int64_t now)
{
	if (input.upper && input.lower) {
		gate_assist(gate, now, true);
	}

	switch (gate->state) {
	case GATE_CLOSED:
		if (!input.lower) {
			gate_assist(gate, now, true);
		}
		break;
	case GATE_OPENING:
		if (now >= gate->deadline) {
			gate_assist(gate, now, true);
		} else if (input.upper) {
			if (set(gate, GATE_ENABLE, false) && set(gate, GATE_RED, false) &&
			    set(gate, GATE_GREEN, true)) {
				gate->state = GATE_OPEN;
			} else {
				gate_assist(gate, now, true);
			}
		}
		break;
	case GATE_OPEN:
		if (!input.upper) {
			gate_assist(gate, now, true);
		}
		break;
	case GATE_CLOSING:
		if (input.beam || now >= gate->deadline) {
			gate_assist(gate, now, true);
		} else if (input.lower) {
			if (set(gate, GATE_ENABLE, false)) {
				gate->state = GATE_CLOSED;
			} else {
				gate_assist(gate, now, true);
			}
		}
		break;
	case GATE_ASSISTANCE:
		if (gate->buzzing && now >= gate->deadline) {
			gate->buzzing = !set(gate, GATE_BUZZER, false);
		}
		break;
	}
}

static bool start_motion(struct gate *gate, int64_t now, bool opening)
{
	if (!set(gate, GATE_GREEN, false) || !set(gate, GATE_RED, true) ||
	    !set(gate, GATE_DIRECTION, opening) || !set(gate, GATE_ENABLE, true)) {
		gate_assist(gate, now, true);
		return false;
	}

	gate->deadline = now + GATE_MOTION_MS;
	gate->state = opening ? GATE_OPENING : GATE_CLOSING;
	return true;
}

bool gate_open(struct gate *gate, struct gate_inputs input, int64_t now)
{
	if (gate->state != GATE_CLOSED) {
		return false;
	}
	gate_step(gate, input, now);
	return gate->state == GATE_CLOSED && start_motion(gate, now, true);
}

bool gate_close(struct gate *gate, struct gate_inputs input, int64_t now)
{
	if (gate->state != GATE_OPEN) {
		return false;
	}
	gate_step(gate, input, now);
	return gate->state == GATE_OPEN && !input.beam && start_motion(gate, now, false);
}

bool gate_rearm(struct gate *gate, struct gate_inputs input, bool vehicle_gone)
{
	if (gate->state != GATE_ASSISTANCE || gate->latched || gate->buzzing ||
	    !vehicle_gone || !input.lower || input.upper || input.beam) {
		return false;
	}

	gate->state = GATE_CLOSED;
	return true;
}
