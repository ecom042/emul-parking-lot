/*
 * Tests of hw/adc4 on an emulated ADC (zephyr,adc-emul, see ../app.overlay).
 * adc_emul_const_raw_value_set() plays the voltage on each channel.
 */
#include <errno.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "hw/adc4.h"

#define CH_ENTRY 1
#define CH_EXIT 2

static const struct device *const adc = DEVICE_DT_GET(DT_NODELABEL(test_adc));

static const struct adc_channel_cfg cfgs[] = {
	{
		.gain = ADC_GAIN_1,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME_DEFAULT,
		.channel_id = CH_ENTRY,
	},
	{
		.gain = ADC_GAIN_1,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME_DEFAULT,
		.channel_id = CH_EXIT,
	},
};

/* Wait (a few ms at most) until the pending conversion finishes. */
static int wait_for_result(uint16_t *code)
{
	int rc = -EAGAIN;

	for (int i = 0; i < 100 && rc == -EAGAIN; i++) {
		rc = adc4_poll(code);
		if (rc == -EAGAIN) {
			k_msleep(1);
		}
	}
	return rc;
}

static void *suite_setup(void)
{
	zassert_ok(adc4_init(adc, cfgs, ARRAY_SIZE(cfgs)));
	return NULL;
}

static void drain(void *fixture)
{
	uint16_t code;

	ARG_UNUSED(fixture);
	if (adc4_busy()) {
		(void)wait_for_result(&code);
	}
}

ZTEST_SUITE(hw_adc4, NULL, suite_setup, NULL, drain, NULL);

ZTEST(hw_adc4, test_a_conversion_returns_the_channel_code)
{
	uint16_t code = 0;

	zassert_ok(adc_emul_const_raw_value_set(adc, CH_ENTRY, 683));
	zassert_ok(adc4_start(CH_ENTRY));
	zassert_ok(wait_for_result(&code));
	zassert_equal(code, 683);
	zassert_false(adc4_busy());
}

ZTEST(hw_adc4, test_each_channel_has_its_own_value)
{
	uint16_t code = 0;

	zassert_ok(adc_emul_const_raw_value_set(adc, CH_ENTRY, 100));
	zassert_ok(adc_emul_const_raw_value_set(adc, CH_EXIT, 2000));
	zassert_ok(adc4_start(CH_EXIT));
	zassert_ok(wait_for_result(&code));
	zassert_equal(code, 2000);
	zassert_ok(adc4_start(CH_ENTRY));
	zassert_ok(wait_for_result(&code));
	zassert_equal(code, 100);
}

ZTEST(hw_adc4, test_a_second_start_is_refused_while_the_request_is_owned)
{
	uint16_t code;

	zassert_ok(adc4_start(CH_ENTRY));
	zassert_true(adc4_busy());
	zassert_equal(adc4_start(CH_EXIT), -EBUSY);
	zassert_ok(wait_for_result(&code));
	zassert_ok(adc4_start(CH_EXIT));
}

ZTEST(hw_adc4, test_poll_without_a_conversion_is_invalid)
{
	uint16_t code;

	zassert_equal(adc4_poll(&code), -EINVAL);
}

ZTEST(hw_adc4, test_a_driver_error_is_reported_and_releases_the_request)
{
	uint16_t code;

	/* Channel 5 was never set up: the driver rejects the read. */
	zassert_true(adc4_start(5) != 0 || wait_for_result(&code) != 0);
	zassert_false(adc4_busy());
}

ZTEST(hw_adc4, test_a_read_refused_by_the_driver_is_reported_at_once)
{
	/* Beyond the emulator's 8 channels: adc_read_async() refuses it. */
	zassert_true(adc4_start(20) != 0);
	zassert_false(adc4_busy(), "a refused read owns nothing");
}

ZTEST(hw_adc4, test_init_reports_a_channel_setup_error)
{
	const struct adc_channel_cfg bad = {
		.gain = ADC_GAIN_1,
		.reference = ADC_REF_INTERNAL,
		.acquisition_time = ADC_ACQ_TIME_DEFAULT,
		.channel_id = 31, /* the emulator only has a few channels */
	};

	zassert_true(adc4_init(adc, &bad, 1) != 0);
	zassert_ok(adc4_init(adc, cfgs, ARRAY_SIZE(cfgs)));
}
