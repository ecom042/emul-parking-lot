#include <string.h>

#include "parking.h"

#define PASSAGE_TIMEOUT_MS 10000
#define NO_SESSION (-1)

enum parking_phase { IDLE, CAPTURING, PAYING, OPENING, PASSING };

struct lane_control {
	enum parking_phase phase;
	char plate[PARKING_PLATE_SIZE];
	int slot;
	int64_t deadline;
	bool crossed_beam;
};

static struct {
	char plate[PARKING_PLATE_SIZE];
	int64_t entered_at;
} sessions[CONFIG_PARKING_CAPACITY];

static struct lane_control lanes[PARKING_LANES];
static const struct parking_equipment *equipment;

void parking_set_equipment(const struct parking_equipment *adapter)
{
	equipment = adapter;
}

static int find_session(const char *plate, int *vacancy)
{
	*vacancy = NO_SESSION;
	for (int i = 0; i < CONFIG_PARKING_CAPACITY; ++i) {
		if (!sessions[i].plate[0]) {
			*vacancy = i;
		} else if (memcmp(sessions[i].plate, plate, PARKING_PLATE_SIZE) == 0) {
			return i;
		}
	}
	return NO_SESSION;
}

static void abandon(enum parking_lane_id id, struct lane_control *lane)
{
	if (lane->phase == CAPTURING) {
		equipment->capture_cancel(id);
	} else if (lane->phase == PAYING) {
		equipment->payment_cancel();
	}
	lane->phase = IDLE;
}

static void open_gate(struct lane_control *lane, struct gate *gate,
		      struct gate_inputs input, int64_t now)
{
	lane->phase = gate_open(gate, input, now) ? OPENING : IDLE;
}

static void recognize(enum parking_lane_id id, struct lane_control *lane,
		      struct gate *gate, struct gate_inputs input, int64_t now)
{
	enum equipment_result result = equipment->capture_poll(id, now, lane->plate);
	int vacancy;

	if (result == EQUIPMENT_PENDING) {
		return;
	}
	if (result == EQUIPMENT_FAILED) {
		abandon(id, lane);
		gate_assist(gate, now, false);
		return;
	}
	lane->phase = IDLE;
	lane->slot = find_session(lane->plate, &vacancy);
	if (id == PARKING_ENTRY) {
		if (lane->slot == NO_SESSION && vacancy != NO_SESSION) {
			lane->slot = vacancy;
			open_gate(lane, gate, input, now);
			return;
		}
	} else if (lane->slot != NO_SESSION &&
		   equipment->payment_start(lane->plate, sessions[lane->slot].entered_at) == 0) {
		lane->phase = PAYING;
		return;
	}
	gate_assist(gate, now, false);
}

static void passage(enum parking_lane_id id, struct lane_control *lane,
		    struct gate *gate, struct gate_inputs input, bool beam_rose,
		    bool departed, int64_t now)
{
	if (now >= lane->deadline) {
		gate_assist(gate, now, true);
		lane->phase = IDLE;
		return;
	}
	lane->crossed_beam |= input.beam || beam_rose;
	if (!lane->crossed_beam || input.beam || !departed) {
		return;
	}
	if (id == PARKING_ENTRY) {
		memcpy(sessions[lane->slot].plate, lane->plate, PARKING_PLATE_SIZE);
		sessions[lane->slot].entered_at = now;
	} else {
		sessions[lane->slot].plate[0] = '\0';
	}
	lane->phase = IDLE;
	gate_close(gate, input, now);
}

void parking_step(enum parking_lane_id id, struct gate *gate,
		  struct gate_inputs input, bool beam_rose, int64_t now)
{
	struct lane_control *lane = &lanes[id];
	uint16_t code;
	bool fresh = distance_read(id, now, &code);
	bool departed = fresh && code > PARKING_DEPARTURE_CODE;
	enum equipment_result result;

	if (distance_faulted() && !gate->latched) {
		gate_assist(gate, now, true);
	}
	if (gate->state == GATE_ASSISTANCE) {
		abandon(id, lane);
		gate_rearm(gate, input, departed);
		return;
	}
	if (equipment == NULL) {
		return;
	}
	if (departed && (lane->phase == CAPTURING || lane->phase == PAYING)) {
		abandon(id, lane);
		return;
	}

	switch (lane->phase) {
	case IDLE:
		if (gate->state == GATE_CLOSED && fresh && code < PARKING_ARRIVAL_CODE) {
			if (equipment->capture_start(id, now) == 0) {
				lane->phase = CAPTURING;
			} else {
				gate_assist(gate, now, false);
			}
		}
		break;
	case CAPTURING:
		if (fresh) {
			recognize(id, lane, gate, input, now);
		}
		break;
	case PAYING:
		if (!fresh) {
			break;
		}
		result = equipment->payment_poll(now);
		if (result == EQUIPMENT_OK) {
			open_gate(lane, gate, input, now);
		} else if (result == EQUIPMENT_FAILED) {
			abandon(id, lane);
			gate_assist(gate, now, false);
		}
		break;
	case OPENING:
		if (gate->state == GATE_OPEN) {
			lane->phase = PASSING;
			lane->deadline = now + PASSAGE_TIMEOUT_MS;
			lane->crossed_beam = input.beam;
		}
		break;
	case PASSING:
		passage(id, lane, gate, input, beam_rose, departed, now);
		break;
	}
}
