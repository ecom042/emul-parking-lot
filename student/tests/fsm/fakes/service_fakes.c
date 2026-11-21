#include "service_fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(int, barrier_init, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(bool, barrier_is_closed, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, barrier_stop, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, signals_init, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, signals_stop, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, signals_go, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, signals_assist, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, camera_init, enum lane_id);
DEFINE_FAKE_VOID_FUNC(camera_poll, enum lane_id, int64_t);
DEFINE_FAKE_VALUE_FUNC(int, presence_init, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, distance_init);
DEFINE_FAKE_VOID_FUNC(distance_start);
DEFINE_FAKE_VOID_FUNC(sessions_clear);
DEFINE_FAKE_VALUE_FUNC(int, barrier_open, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, camera_start, enum lane_id);
DEFINE_FAKE_VOID_FUNC(camera_abandon, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, camera_plate, enum lane_id, char *);
DEFINE_FAKE_VALUE_FUNC(bool, sessions_can_enter, const char *);
DEFINE_FAKE_VALUE_FUNC(bool, presence_car_far, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(bool, presence_beam_active, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(bool, presence_car_near, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, barrier_close, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, sessions_create, const char *, int64_t);
DEFINE_FAKE_VALUE_FUNC(int, sessions_entry_time, const char *, int64_t *);
DEFINE_FAKE_VALUE_FUNC(int, payment_init);
DEFINE_FAKE_VOID_FUNC(payment_cancel);
DEFINE_FAKE_VOID_FUNC(payment_poll, int64_t);
DEFINE_FAKE_VALUE_FUNC(int, screen_init);

void service_fakes_reset(void)
{
	RESET_FAKE(barrier_init);
	RESET_FAKE(barrier_is_closed);
	RESET_FAKE(barrier_stop);
	RESET_FAKE(signals_init);
	RESET_FAKE(signals_stop);
	RESET_FAKE(signals_go);
	RESET_FAKE(signals_assist);
	RESET_FAKE(camera_init);
	RESET_FAKE(camera_poll);
	RESET_FAKE(presence_init);
	RESET_FAKE(distance_init);
	RESET_FAKE(distance_start);
	RESET_FAKE(sessions_clear);
	RESET_FAKE(barrier_open);
	RESET_FAKE(camera_start);
	RESET_FAKE(camera_abandon);
	RESET_FAKE(camera_plate);
	RESET_FAKE(sessions_can_enter);
	RESET_FAKE(presence_car_far);
	RESET_FAKE(presence_beam_active);
	RESET_FAKE(presence_car_near);
	RESET_FAKE(barrier_close);
	RESET_FAKE(sessions_create);
	RESET_FAKE(sessions_entry_time);
	RESET_FAKE(payment_init);
	RESET_FAKE(payment_cancel);
	RESET_FAKE(payment_poll);
	RESET_FAKE(screen_init);
	FFF_RESET_HISTORY();
}
