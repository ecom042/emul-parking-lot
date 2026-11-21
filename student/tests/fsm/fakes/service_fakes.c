#include "service_fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(int, barrier_init, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(bool, barrier_is_closed, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, barrier_stop, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, signals_init, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, signals_stop, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, signals_assist, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, camera_init, enum lane_id);
DEFINE_FAKE_VOID_FUNC(camera_poll, enum lane_id, int64_t);
DEFINE_FAKE_VALUE_FUNC(int, presence_init, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, distance_init);
DEFINE_FAKE_VOID_FUNC(distance_start);

void service_fakes_reset(void)
{
	RESET_FAKE(barrier_init);
	RESET_FAKE(barrier_is_closed);
	RESET_FAKE(barrier_stop);
	RESET_FAKE(signals_init);
	RESET_FAKE(signals_stop);
	RESET_FAKE(signals_assist);
	RESET_FAKE(camera_init);
	RESET_FAKE(camera_poll);
	RESET_FAKE(presence_init);
	RESET_FAKE(distance_init);
	RESET_FAKE(distance_start);
	FFF_RESET_HISTORY();
}
