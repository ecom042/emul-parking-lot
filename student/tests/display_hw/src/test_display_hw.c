#include <errno.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(bool, fake_spi_ready, const struct spi_dt_spec *);
FAKE_VALUE_FUNC(bool, fake_gpio_ready, const struct gpio_dt_spec *);
FAKE_VALUE_FUNC(int, fake_gpio_configure, const struct gpio_dt_spec *, gpio_flags_t);
FAKE_VALUE_FUNC(int, fake_gpio_set, const struct gpio_dt_spec *, int);
FAKE_VALUE_FUNC(int, fake_spi_write, const struct spi_dt_spec *, const struct spi_buf_set *);

/* Keep the real devicetree choices and replace the driver calls in this suite. */
#define spi_is_ready_dt fake_spi_ready
#define gpio_is_ready_dt fake_gpio_ready
#define gpio_pin_configure_dt fake_gpio_configure
#define gpio_pin_set_dt fake_gpio_set
#define spi_write_dt fake_spi_write
#include "../../../src/payment_display_hw.c"
#undef spi_write_dt
#undef gpio_pin_set_dt
#undef gpio_pin_configure_dt
#undef gpio_is_ready_dt
#undef spi_is_ready_dt

static uint8_t data_bytes[] = { 0x07, 0xff, 0, 0 };

static int check_transfer(const struct spi_dt_spec *spec, const struct spi_buf_set *tx)
{
	zassert_equal_ptr(spec, &spi);
	zassert_equal(tx->count, 1);
	zassert_equal_ptr(tx->buffers[0].buf, data_bytes);
	zassert_equal(tx->buffers[0].len, sizeof(data_bytes));
	zassert_equal_ptr(fake_gpio_set_fake.arg0_val, &dc);
	zassert_true(fff.call_history[0] == (fff_function_t)fake_gpio_set);
	zassert_true(fff.call_history[1] == (fff_function_t)fake_spi_write);
	return fake_spi_write_fake.return_val;
}

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(fake_spi_ready);
	RESET_FAKE(fake_gpio_ready);
	RESET_FAKE(fake_gpio_configure);
	RESET_FAKE(fake_gpio_set);
	RESET_FAKE(fake_spi_write);
	FFF_RESET_HISTORY();
	fake_spi_ready_fake.return_val = true;
	fake_gpio_ready_fake.return_val = true;
	fake_spi_write_fake.custom_fake = check_transfer;
}

ZTEST(parking_display_hw, test_spi_unavailable_leaves_gpio_untouched)
{
	fake_spi_ready_fake.return_val = false;
	zassert_equal(payment_display_hw_prepare(), -ENODEV);
	zassert_equal_ptr(fake_spi_ready_fake.arg0_val, &spi);
	zassert_equal(fake_gpio_ready_fake.call_count, 0);
	zassert_equal(fake_gpio_configure_fake.call_count, 0);
}

ZTEST(parking_display_hw, test_dc_unavailable_prevents_gpio_configuration)
{
	fake_gpio_ready_fake.return_val = false;
	zassert_equal(payment_display_hw_prepare(), -ENODEV);
	zassert_equal(fake_gpio_ready_fake.call_count, 1);
	zassert_equal_ptr(fake_gpio_ready_fake.arg0_val, &dc);
	zassert_equal(fake_gpio_configure_fake.call_count, 0);
}

ZTEST(parking_display_hw, test_reset_unavailable_prevents_gpio_configuration)
{
	bool ready[] = { true, false };

	SET_RETURN_SEQ(fake_gpio_ready, ready, ARRAY_SIZE(ready));
	zassert_equal(payment_display_hw_prepare(), -ENODEV);
	zassert_equal(fake_gpio_ready_fake.call_count, 2);
	zassert_equal_ptr(fake_gpio_ready_fake.arg0_val, &reset);
	zassert_equal(fake_gpio_configure_fake.call_count, 0);
}

ZTEST(parking_display_hw, test_dc_configuration_failure_skips_reset_configuration)
{
	fake_gpio_configure_fake.return_val = -EIO;
	zassert_equal(payment_display_hw_prepare(), -EIO);
	zassert_equal(fake_gpio_configure_fake.call_count, 1);
	zassert_equal_ptr(fake_gpio_configure_fake.arg0_val, &dc);
	zassert_equal(fake_gpio_configure_fake.arg1_val, GPIO_OUTPUT_INACTIVE);
}

ZTEST(parking_display_hw, test_reset_configuration_failure_is_returned)
{
	int results[] = { 0, -EIO };

	SET_RETURN_SEQ(fake_gpio_configure, results, ARRAY_SIZE(results));
	zassert_equal(payment_display_hw_prepare(), -EIO);
	zassert_equal(fake_gpio_configure_fake.call_count, 2);
	zassert_equal_ptr(fake_gpio_configure_fake.arg0_val, &reset);
	zassert_equal(fake_gpio_configure_fake.arg1_val, GPIO_OUTPUT_INACTIVE);
}

ZTEST(parking_display_hw, test_reset_setup_is_inactive_and_set_error_is_returned)
{
	zassert_ok(payment_display_hw_prepare());
	zassert_equal(fake_gpio_configure_fake.call_count, 2);
	zassert_equal_ptr(fake_gpio_configure_fake.arg0_val, &reset);
	zassert_equal(fake_gpio_configure_fake.arg1_val, GPIO_OUTPUT_INACTIVE);

	fake_gpio_set_fake.return_val = -EIO;
	zassert_equal(payment_display_hw_reset(true), -EIO);
	zassert_equal_ptr(fake_gpio_set_fake.arg0_val, &reset);
	zassert_equal(fake_gpio_set_fake.arg1_val, 1);
}

ZTEST(parking_display_hw, test_dc_set_error_prevents_spi_transaction)
{
	fake_gpio_set_fake.return_val = -EIO;
	zassert_equal(payment_display_hw_write(false, data_bytes, sizeof(data_bytes)), -EIO);
	zassert_equal_ptr(fake_gpio_set_fake.arg0_val, &dc);
	zassert_equal(fake_gpio_set_fake.arg1_val, 0);
	zassert_equal(fake_spi_write_fake.call_count, 0);
}

ZTEST(parking_display_hw, test_spi_error_preserves_driver_status)
{
	fake_spi_write_fake.return_val = -EIO;
	zassert_equal(payment_display_hw_write(true, data_bytes, sizeof(data_bytes)), -EIO);
	zassert_equal(fake_gpio_set_fake.arg1_val, 1);
	zassert_equal(fake_spi_write_fake.call_count, 1);
}

ZTEST_SUITE(parking_display_hw, NULL, NULL, before_each, NULL, NULL);
