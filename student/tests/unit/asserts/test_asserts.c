/*
 * Programming errors: an invalid lane id must trip the module's __ASSERT
 * (CONFIG_ASSERT=y in the firmware) instead of indexing past an array.
 *
 * CONFIG_ZTEST_ASSERT_HOOK turns an expected assert into the end of the test
 * thread instead of a halt, so reaching the end of a test body means the
 * assert did not fire.
 */
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>

#include "fsm/lanes.h"
#include "hw/lane_hw.h"
#include "services/camera.h"

/* One past the last lane: the smallest invalid id. */
#define INVALID_LANE ((enum lane_id)LANE_COUNT)

ZTEST_SUITE(asserts, NULL, NULL, NULL, NULL, NULL);

ZTEST(asserts, test_lanes_get_rejects_an_invalid_lane)
{
	ztest_set_assert_valid(true);
	(void)lanes_get(INVALID_LANE);

	ztest_test_fail(); /* not reached when the assert fires */
}

ZTEST(asserts, test_lane_hw_get_rejects_an_invalid_lane)
{
	ztest_set_assert_valid(true);
	(void)lane_hw_get(INVALID_LANE);

	ztest_test_fail(); /* not reached when the assert fires */
}

ZTEST(asserts, test_camera_rejects_an_invalid_lane)
{
	ztest_set_assert_valid(true);
	(void)camera_start(INVALID_LANE);

	ztest_test_fail(); /* not reached when the assert fires */
}
