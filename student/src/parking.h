#ifndef PARKING_CONTROLLER_H
#define PARKING_CONTROLLER_H

#include "distance.h"
#include "gate.h"

#define PARKING_PLATE_SIZE 8

enum parking_lane_id { PARKING_ENTRY, PARKING_EXIT };
enum equipment_result { EQUIPMENT_PENDING, EQUIPMENT_OK, EQUIPMENT_FAILED };

/*
 * These operations run in the serialized controller queue and must return
 * promptly. The equipment implementation owns the wire identifiers, retries,
 * framing and deadlines. Poll reports only the current operation; cancel
 * invalidates it before returning and queues any required UART transmission.
 */
struct parking_equipment {
	int (*capture_start)(enum parking_lane_id lane, int64_t now);
	enum equipment_result (*capture_poll)(enum parking_lane_id lane, int64_t now,
					      char plate[PARKING_PLATE_SIZE]);
	void (*capture_cancel)(enum parking_lane_id lane);
	/* Render the plate, freeze the tariff from the entry time, then charge. */
	int (*payment_start)(const char plate[PARKING_PLATE_SIZE], int64_t entered_at);
	enum equipment_result (*payment_poll)(int64_t now);
	void (*payment_cancel)(void);
};

/* Supply the complete, static-lifetime equipment adapter during boot. */
void parking_set_equipment(const struct parking_equipment *equipment);

void parking_step(enum parking_lane_id lane, struct gate *gate,
		  struct gate_inputs input, bool beam_rose, int64_t now);

#endif
