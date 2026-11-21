#ifndef PARKING_TESTS_FSM_SERVICE_FAKES_H_
#define PARKING_TESTS_FSM_SERVICE_FAKES_H_

/*
 * FFF fakes of every service fsm/ calls, shared by the tests of this suite.
 * A fake can be defined only once per binary, so they live in
 * service_fakes.c and every test file includes this header.
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/fff.h>

#include "hw/lane_hw.h"

DECLARE_FAKE_VALUE_FUNC(int, barrier_init, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(bool, barrier_is_closed, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, barrier_stop, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, signals_init, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, signals_stop, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, signals_assist, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, camera_init, enum lane_id);
DECLARE_FAKE_VOID_FUNC(camera_poll, enum lane_id, int64_t);

/** Reset every fake above and the shared call history. */
void service_fakes_reset(void);

#endif /* PARKING_TESTS_FSM_SERVICE_FAKES_H_ */
