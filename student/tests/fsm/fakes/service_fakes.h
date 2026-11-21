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
#include "services/wire.h" /* PLATE_LEN */

DECLARE_FAKE_VALUE_FUNC(int, barrier_init, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(bool, barrier_is_closed, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, barrier_stop, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, signals_init, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, signals_stop, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, signals_go, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, signals_assist, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, camera_init, enum lane_id);
DECLARE_FAKE_VOID_FUNC(camera_poll, enum lane_id, int64_t);
DECLARE_FAKE_VALUE_FUNC(int, presence_init, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, distance_init);
DECLARE_FAKE_VOID_FUNC(distance_start);
DECLARE_FAKE_VOID_FUNC(sessions_clear);
DECLARE_FAKE_VALUE_FUNC(int, barrier_open, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, camera_start, enum lane_id);
DECLARE_FAKE_VOID_FUNC(camera_abandon, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, camera_plate, enum lane_id, char *);
DECLARE_FAKE_VALUE_FUNC(bool, sessions_can_enter, const char *);
DECLARE_FAKE_VALUE_FUNC(bool, presence_car_far, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(bool, presence_beam_active, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(bool, presence_car_near, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, barrier_close, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, sessions_create, const char *, int64_t);
DECLARE_FAKE_VALUE_FUNC(int, sessions_entry_time, const char *, int64_t *);
DECLARE_FAKE_VALUE_FUNC(int, payment_init);
DECLARE_FAKE_VOID_FUNC(payment_cancel);
DECLARE_FAKE_VOID_FUNC(payment_poll, int64_t);
DECLARE_FAKE_VALUE_FUNC(int, screen_init);
DECLARE_FAKE_VALUE_FUNC(int, screen_show_plate, const char *);
DECLARE_FAKE_VALUE_FUNC(int, screen_show_amount, uint32_t);
DECLARE_FAKE_VALUE_FUNC(int, tariff_cents, int64_t, uint32_t *);
DECLARE_FAKE_VALUE_FUNC(int, payment_start, const char *, uint32_t);

/** Reset every fake above and the shared call history. */
void service_fakes_reset(void);

#endif /* PARKING_TESTS_FSM_SERVICE_FAKES_H_ */
