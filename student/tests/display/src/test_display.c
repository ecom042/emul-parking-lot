#include <errno.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "parking_tariff.h"
#include "payment_display.h"
#include "payment_display_hw.h"

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, payment_display_hw_prepare);
FAKE_VALUE_FUNC(int, payment_display_hw_reset, bool);
FAKE_VALUE_FUNC(int, payment_display_hw_write, bool, uint8_t *, size_t);

struct transfer {
	bool data;
	size_t len;
	uint8_t prefix[8];
};

static struct transfer transfers[64];
static size_t transfer_count;
static size_t fail_on_transfer;

static int record_write(bool data, uint8_t *bytes, size_t len)
{
	zassert_true(transfer_count < ARRAY_SIZE(transfers));
	struct transfer *transfer = &transfers[transfer_count++];

	transfer->data = data;
	transfer->len = len;
	memcpy(transfer->prefix, bytes, MIN(sizeof(transfer->prefix), len));
	return transfer_count == fail_on_transfer ? -EIO : 0;
}

static void reset_fakes(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(payment_display_hw_prepare);
	RESET_FAKE(payment_display_hw_reset);
	RESET_FAKE(payment_display_hw_write);
	FFF_RESET_HISTORY();
	transfer_count = 0;
	fail_on_transfer = 0;
	payment_display_hw_write_fake.custom_fake = record_write;
}

static void assert_command(size_t index, uint8_t cmd)
{
	zassert_false(transfers[index].data);
	zassert_equal(transfers[index].len, 1);
	zassert_equal(transfers[index].prefix[0], cmd);
}

static void assert_data(size_t index, const uint8_t *bytes, size_t len)
{
	zassert_true(transfers[index].data);
	zassert_equal(transfers[index].len, len);
	zassert_mem_equal(transfers[index].prefix, bytes, MIN(sizeof(transfers[index].prefix), len));
}

ZTEST(parking_display, test_init_has_reset_pulse_delays_and_spi_sequence)
{
	int64_t started = k_uptime_get();

	zassert_ok(payment_display_init());
	zassert_true(k_uptime_get() - started >= 400);
	zassert_equal(payment_display_hw_prepare_fake.call_count, 1);
	zassert_equal(payment_display_hw_reset_fake.call_count, 2);
	zassert_true(payment_display_hw_reset_fake.arg0_history[0]);
	zassert_false(payment_display_hw_reset_fake.arg0_history[1]);
	zassert_equal(transfer_count, 7);
	assert_command(0, 0x01);
	assert_command(1, 0x11);
	assert_command(2, 0x3a);
	assert_data(3, (uint8_t[]){ 0x55 }, 1);
	assert_command(4, 0x36);
	assert_data(5, (uint8_t[]){ 0x00 }, 1);
	assert_command(6, 0x29);
}

ZTEST(parking_display, test_init_stops_after_prepare_or_reset_error)
{
	payment_display_hw_prepare_fake.return_val = -ENODEV;
	zassert_equal(payment_display_init(), -ENODEV);
	zassert_equal(payment_display_hw_reset_fake.call_count, 0);
	zassert_equal(transfer_count, 0);

	payment_display_hw_prepare_fake.return_val = 0;
	payment_display_hw_reset_fake.return_val = -EIO;
	zassert_equal(payment_display_init(), -EIO);
	zassert_equal(payment_display_hw_reset_fake.call_count, 1);
	zassert_equal(transfer_count, 0);

	int reset_results[] = { 0, -EIO };

	SET_RETURN_SEQ(payment_display_hw_reset, reset_results, ARRAY_SIZE(reset_results));
	zassert_equal(payment_display_init(), -EIO);
	zassert_equal(payment_display_hw_reset_fake.call_count, 3);
	zassert_equal(transfer_count, 0);
}

ZTEST(parking_display, test_init_stops_when_color_parameter_cannot_be_written)
{
	fail_on_transfer = 4;
	zassert_equal(payment_display_init(), -EIO);
	zassert_equal(transfer_count, 4);
	assert_command(2, 0x3a);
}

ZTEST(parking_display, test_plate_sends_address_window_and_big_endian_pixels)
{
	zassert_ok(payment_display_show_plate("ABC1D23"));
	zassert_equal(transfer_count, 19);
	assert_command(0, 0x2a);
	assert_data(1, (uint8_t[]){ 0, 18, 0, 101 }, 4);
	assert_command(2, 0x2b);
	assert_data(3, (uint8_t[]){ 0, 36, 0, 49 }, 4);
	assert_command(4, 0x2c);
	uint8_t prefix[8] = { 0, 0, 0, 0, 0x07, 0xff, 0x07, 0xff };

	zassert_equal(transfers[5].len, 168);
	zassert_true(transfers[5].data);
	zassert_mem_equal(transfers[5].prefix, prefix, sizeof(prefix));
	zassert_mem_equal(transfers[6].prefix, prefix, sizeof(prefix));
}

ZTEST(parking_display, test_amount_includes_two_status_regions)
{
	zassert_ok(payment_display_show_amount(1000000));
	zassert_equal(transfer_count, 57);
	assert_data(3, (uint8_t[]){ 0, 108, 0, 121 }, 4);
	assert_data(19 + 3, (uint8_t[]){ 0, 170, 0, 183 }, 4);
	assert_data(38 + 3, (uint8_t[]){ 0, 196, 0, 209 }, 4);
	zassert_equal(transfers[5].len, 288);
	zassert_equal(transfers[19 + 5].len, 408);
	zassert_equal(transfers[38 + 5].len, 168);
}

ZTEST(parking_display, test_invalid_text_causes_no_spi_transfer)
{
	zassert_equal(payment_display_show_plate(NULL), -EINVAL);
	zassert_equal(payment_display_show_plate("AB!1D23"), -EINVAL);
	zassert_equal(payment_display_show_amount(0), -EINVAL);
	zassert_equal(payment_display_show_amount(1000001), -EINVAL);
	zassert_equal(transfer_count, 0);
}

ZTEST(parking_display, test_amount_propagates_window_and_pixel_write_errors)
{
	const size_t failure_positions[] = { 1, 3, 5, 6, 20, 39 };

	for (size_t i = 0; i < ARRAY_SIZE(failure_positions); i++) {
		transfer_count = 0;
		fail_on_transfer = failure_positions[i];
		zassert_equal(payment_display_show_amount(200), -EIO);
		zassert_equal(transfer_count, fail_on_transfer);
	}
}

ZTEST(parking_display, test_tariff_rounds_up_after_first_unit)
{
	const int64_t first_unit = CONFIG_PARKING_TARIFF_UNIT_MS;
	uint32_t cents = 0;

	zassert_ok(parking_tariff(11, 11, &cents));
	zassert_equal(cents, 200);
	zassert_ok(parking_tariff(11, 11 + first_unit - 1, &cents));
	zassert_equal(cents, 200);
	zassert_ok(parking_tariff(11, 11 + first_unit, &cents));
	zassert_equal(cents, 200);
	zassert_ok(parking_tariff(11, 11 + first_unit + 1, &cents));
	zassert_equal(cents, 300);
}

ZTEST(parking_display, test_tariff_limit_and_bad_inputs)
{
	const int64_t last_valid = 9999LL * CONFIG_PARKING_TARIFF_UNIT_MS;
	uint32_t cents = 0;

	zassert_equal(parking_tariff(0, 0, NULL), -EINVAL);
	zassert_equal(parking_tariff(-1, 0, &cents), -EINVAL);
	zassert_equal(parking_tariff(1, 0, &cents), -EINVAL);
	zassert_ok(parking_tariff(0, last_valid, &cents));
	zassert_equal(cents, 1000000);
	zassert_equal(parking_tariff(0, last_valid + 1, &cents), -ERANGE);
	zassert_equal(parking_tariff(0, INT64_MAX, &cents), -ERANGE);
	zassert_equal(cents, 1000000);
}

ZTEST_SUITE(parking_display, NULL, NULL, reset_fakes, NULL, NULL);
