#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "distance.h"

ZTEST_SUITE(distance_adc4, NULL, NULL, NULL, NULL, NULL);

ZTEST(distance_adc4, test_reads_both_channels_by_async_driver)
{
	uint16_t entry = 0;
	uint16_t exit_code = 0;
	int64_t started = k_uptime_get();

	zassert_ok(distance_init());
	zassert_false(distance_read(0, started, &entry));
	for (int64_t now = k_uptime_get(); now - started < PARKING_ADC_DEADLINE_MS;
	     now = k_uptime_get()) {
		distance_step(now);
		zassert_false(distance_faulted());
		if (distance_read(0, now, &entry) && distance_read(1, now, &exit_code)) {
			zassert_true(entry > PARKING_DEPARTURE_CODE);
			zassert_true(exit_code > PARKING_DEPARTURE_CODE);
			zassert_false(distance_read(0, now + PARKING_ADC_DEADLINE_MS, &entry));
			return;
		}
		k_sleep(K_MSEC(5));
	}
	zassert_unreachable("Expected fresh samples for both channels within 100 ms");
}
