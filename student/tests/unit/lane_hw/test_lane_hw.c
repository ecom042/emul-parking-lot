/* Tests of hw/lane_hw: the devicetree equipment each lane and the terminal get. */
#include <zephyr/ztest.h>

#include "hw/lane_hw.h"

ZTEST_SUITE(lane_hw, NULL, NULL, NULL, NULL, NULL);

ZTEST(lane_hw, test_each_lane_has_its_own_camera_uart)
{
	zassert_equal(lane_hw_get(LANE_ENTRY)->camera, DEVICE_DT_GET(DT_ALIAS(entry_camera)));
	zassert_equal(lane_hw_get(LANE_EXIT)->camera, DEVICE_DT_GET(DT_ALIAS(exit_camera)));
	zassert_not_equal(lane_hw_get(LANE_ENTRY)->camera, lane_hw_get(LANE_EXIT)->camera);
}

ZTEST(lane_hw, test_distance_channels_are_17_for_entry_and_18_for_exit)
{
	zassert_equal(lane_hw_get(LANE_ENTRY)->distance_cfg.channel_id, 17);
	zassert_equal(lane_hw_get(LANE_EXIT)->distance_cfg.channel_id, 18);
}

ZTEST(lane_hw, test_payment_terminal_is_lpuart1)
{
	zassert_equal(payment_terminal_get(), DEVICE_DT_GET(DT_ALIAS(payment_terminal)));
}

ZTEST(lane_hw, test_distance_adc_is_adc4)
{
	zassert_equal(distance_adc_get(), DEVICE_DT_GET(DT_NODELABEL(adc4)));
}

ZTEST(lane_hw, test_payment_display_is_spi1_with_dc_pd15_and_reset_pe0)
{
	const struct st7789 *lcd = payment_display_get();

	zassert_equal_ptr(lcd->spi.bus, DEVICE_DT_GET(DT_BUS(DT_ALIAS(payment_display))));
	zassert_equal(lcd->dc.pin, 15);
	zassert_equal(lcd->reset.pin, 0);
}
