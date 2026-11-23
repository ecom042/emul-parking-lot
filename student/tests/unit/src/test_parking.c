#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "parking.h"
#include "distance.h"
#include "gate.h"

FAKE_VALUE_FUNC(bool, distance_read, unsigned int, int64_t, uint16_t *);
FAKE_VALUE_FUNC(bool, distance_faulted);
FAKE_VALUE_FUNC(int, capture_start, enum parking_lane_id, int64_t);
FAKE_VOID_FUNC(capture_cancel, enum parking_lane_id);
FAKE_VALUE_FUNC(int, payment_start, const char *, int64_t);
FAKE_VALUE_FUNC(enum equipment_result, payment_poll, int64_t);
FAKE_VOID_FUNC(payment_cancel);
FAKE_VALUE_FUNC(int, motor_write, void *, enum gate_output, bool);

static uint16_t codes[PARKING_LANES];
static const char plate[PARKING_PLATE_SIZE] = "ABC1D23";

static bool read_code(unsigned int lane, int64_t now, uint16_t *code)
{
	ARG_UNUSED(now);
	*code = codes[lane];
	return true;
}

static enum equipment_result accept_plate(enum parking_lane_id lane, int64_t now,
					   char captured[PARKING_PLATE_SIZE])
{
	ARG_UNUSED(lane);
	ARG_UNUSED(now);
	memcpy(captured, plate, sizeof(plate));
	return EQUIPMENT_OK;
}

static const struct parking_equipment adapter = {
	.capture_start = capture_start,
	.capture_poll = accept_plate,
	.capture_cancel = capture_cancel,
	.payment_start = payment_start,
	.payment_poll = payment_poll,
	.payment_cancel = payment_cancel,
};

static void tick(enum parking_lane_id id, struct gate *gate, struct gate_inputs input,
		 uint16_t code, int64_t now)
{
	codes[id] = code;
	gate_step(gate, input, now);
	parking_step(id, gate, input, false, now);
}

ZTEST_SUITE(parking_flow, NULL, NULL, NULL, NULL, NULL);

ZTEST(parking_flow, test_passage_records_entry_and_paid_exit_frees_slot)
{
	struct gate entry = { .write = motor_write };
	struct gate exit_gate = { .write = motor_write };
	struct gate_inputs closed = { .lower = true };
	struct gate_inputs open = { .upper = true };
	struct gate_inputs beam = { .upper = true, .beam = true };

	distance_read_fake.custom_fake = read_code;
	parking_set_equipment(&adapter);

	tick(PARKING_ENTRY, &entry, closed, 682, 0);
	zassert_equal(capture_start_fake.call_count, 1);
	tick(PARKING_ENTRY, &entry, closed, 1025, 10);
	zassert_equal(capture_cancel_fake.call_count, 1);
	tick(PARKING_ENTRY, &entry, closed, 682, 20);
	tick(PARKING_ENTRY, &entry, closed, 682, 25);
	zassert_equal(entry.state, GATE_OPENING);
	tick(PARKING_ENTRY, &entry, open, 682, 2025);
	tick(PARKING_ENTRY, &entry, open, 1025, 2030);
	zassert_equal(entry.state, GATE_OPEN);
	tick(PARKING_ENTRY, &entry, beam, 1025, 2040);
	zassert_equal(entry.state, GATE_OPEN);
	tick(PARKING_ENTRY, &entry, open, 1025, 2050);
	zassert_equal(entry.state, GATE_CLOSING);

	tick(PARKING_EXIT, &exit_gate, closed, 682, 2060);
	tick(PARKING_EXIT, &exit_gate, closed, 682, 2070);
	zassert_equal(payment_start_fake.call_count, 1);
	zassert_mem_equal(payment_start_fake.arg0_val, plate, sizeof(plate));
	zassert_equal(payment_start_fake.arg1_val, 2050);
	tick(PARKING_EXIT, &exit_gate, closed, 682, 2080);
	zassert_equal(exit_gate.state, GATE_CLOSED);
	payment_poll_fake.return_val = EQUIPMENT_OK;
	tick(PARKING_EXIT, &exit_gate, closed, 682, 2090);
	zassert_equal(exit_gate.state, GATE_OPENING);
	tick(PARKING_EXIT, &exit_gate, open, 682, 4090);
	tick(PARKING_EXIT, &exit_gate, beam, 682, 4100);
	tick(PARKING_EXIT, &exit_gate, open, 1025, 4110);
	zassert_equal(exit_gate.state, GATE_CLOSING);

	tick(PARKING_ENTRY, &entry, closed, 1025, 4120);
	tick(PARKING_ENTRY, &entry, closed, 682, 4130);
	tick(PARKING_ENTRY, &entry, closed, 682, 4140);
	zassert_equal(entry.state, GATE_OPENING);
}
