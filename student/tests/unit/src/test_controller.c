/*
 * test_controller.c — Comprehensive ztest + FFF unit test suite
 *
 * Tests all firmware logic modules (session, font, adc, camera, payment,
 * display, lane, controller) in isolation from real drivers by faking hw.h,
 * exercising both normal requirements and driver error paths that Renode
 * equipment models never inject.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "adc.h"
#include "camera.h"
#include "controller.h"
#include "display.h"
#include "font.h"
#include "hw.h"
#include "lane.h"
#include "payment.h"
#include "session.h"

DEFINE_FFF_GLOBALS;

/* ── FFF fakes for hw.h ─────────────────────────────────────────── */

FAKE_VALUE_FUNC(bool, hw_out_is_ready, hw_out_pin_t);
FAKE_VALUE_FUNC(int, hw_out_configure_inactive, hw_out_pin_t);
FAKE_VALUE_FUNC(int, hw_out_set, hw_out_pin_t, bool);

FAKE_VALUE_FUNC(bool, hw_in_is_ready, hw_in_pin_t);
FAKE_VALUE_FUNC(int, hw_in_configure, hw_in_pin_t);
FAKE_VOID_FUNC(hw_exti_set_work_handler, hw_work_fn_t);
FAKE_VALUE_FUNC(int, hw_in_register_callback, hw_in_pin_t);
FAKE_VALUE_FUNC(int, hw_in_configure_interrupt, hw_in_pin_t);
FAKE_VALUE_FUNC(int, hw_in_get, hw_in_pin_t);

FAKE_VALUE_FUNC(unsigned int, hw_irq_lock);
FAKE_VOID_FUNC(hw_irq_unlock, unsigned int);
FAKE_VALUE_FUNC(int64_t, hw_uptime_ms);
FAKE_VOID_FUNC(hw_wait_tick);

FAKE_VALUE_FUNC(bool, hw_uart_is_ready, hw_uart_id_t);
FAKE_VALUE_FUNC(int, hw_uart_poll_in, hw_uart_id_t, uint8_t *);
FAKE_VOID_FUNC(hw_uart_poll_out, hw_uart_id_t, uint8_t);

FAKE_VALUE_FUNC(bool, hw_adc_is_ready);
FAKE_VALUE_FUNC(int, hw_adc_setup_channel, lane_id_t);
FAKE_VALUE_FUNC(int, hw_adc_start_async, lane_id_t);
FAKE_VOID_FUNC(hw_adc_check_async, unsigned int *, int *, int16_t *);

FAKE_VALUE_FUNC(bool, hw_spi_is_ready);
FAKE_VALUE_FUNC(int, hw_spi_write, uint8_t *, size_t);

/* ── Custom fake state helpers ──────────────────────────────────── */

static int sim_in_levels[HW_IN_COUNT];
static bool sim_out_levels[HW_OUT_COUNT];
static hw_out_pin_t fail_out_pin = HW_OUT_COUNT;
static int fail_out_Countdown = -1;

static unsigned int sim_adc_signaled;
static int sim_adc_result;
static int16_t sim_adc_raw_lanes[LANE_COUNT];
static lane_id_t last_adc_lane;

static uint8_t sim_uart_rx_buf[HW_UART_COUNT][2048];
static size_t sim_uart_rx_head[HW_UART_COUNT];
static size_t sim_uart_rx_tail[HW_UART_COUNT];

static char sim_uart_tx_buf[HW_UART_COUNT][1024];
static size_t sim_uart_tx_len[HW_UART_COUNT];

static int fail_spi_on_call = -1;
static int spi_call_counter;

static int custom_hw_in_get(hw_in_pin_t pin)
{
	return sim_in_levels[pin];
}

static int custom_hw_out_set(hw_out_pin_t pin, bool active)
{
	if (pin == fail_out_pin) {
		if (fail_out_Countdown <= 0) {
			return -EIO;
		}
		fail_out_Countdown--;
	}
	sim_out_levels[pin] = active;
	return 0;
}

static int custom_hw_adc_start_async(lane_id_t lane)
{
	last_adc_lane = lane;
	return 0;
}

static void custom_hw_adc_check_async(unsigned int *signaled, int *result,
				      int16_t *raw_sample)
{
	*signaled = sim_adc_signaled;
	*result = sim_adc_result;
	*raw_sample = sim_adc_raw_lanes[last_adc_lane];
}

static int custom_hw_uart_poll_in(hw_uart_id_t id, uint8_t *byte)
{
	if (sim_uart_rx_head[id] >= sim_uart_rx_tail[id]) {
		sim_uart_rx_head[id] = 0U;
		sim_uart_rx_tail[id] = 0U;
		return -1;
	}
	*byte = sim_uart_rx_buf[id][sim_uart_rx_head[id]++];
	if (sim_uart_rx_head[id] == sim_uart_rx_tail[id]) {
		sim_uart_rx_head[id] = 0U;
		sim_uart_rx_tail[id] = 0U;
	}
	return 0;
}

static void custom_hw_uart_poll_out(hw_uart_id_t id, uint8_t byte)
{
	if (sim_uart_tx_len[id] + 1U < sizeof(sim_uart_tx_buf[id])) {
		sim_uart_tx_buf[id][sim_uart_tx_len[id]++] = (char)byte;
		sim_uart_tx_buf[id][sim_uart_tx_len[id]] = '\0';
	}
}

static int custom_hw_spi_write(uint8_t *data, size_t len)
{
	(void)data;
	(void)len;
	spi_call_counter++;
	if (fail_spi_on_call > 0 && spi_call_counter >= fail_spi_on_call) {
		return -EIO;
	}
	return 0;
}

static void sim_uart_push_rx(hw_uart_id_t id, const char *s)
{
	size_t n = strlen(s);

	for (size_t i = 0U; i < n; i++) {
		if (sim_uart_rx_tail[id] < sizeof(sim_uart_rx_buf[id])) {
			sim_uart_rx_buf[id][sim_uart_rx_tail[id]++] = (uint8_t)s[i];
		}
	}
}

static void reset_all_fakes(void *fixture)
{
	(void)fixture;

	RESET_FAKE(hw_out_is_ready);
	RESET_FAKE(hw_out_configure_inactive);
	RESET_FAKE(hw_out_set);
	RESET_FAKE(hw_in_is_ready);
	RESET_FAKE(hw_in_configure);
	RESET_FAKE(hw_exti_set_work_handler);
	RESET_FAKE(hw_in_register_callback);
	RESET_FAKE(hw_in_configure_interrupt);
	RESET_FAKE(hw_in_get);
	RESET_FAKE(hw_irq_lock);
	RESET_FAKE(hw_irq_unlock);
	RESET_FAKE(hw_uptime_ms);
	RESET_FAKE(hw_wait_tick);
	RESET_FAKE(hw_uart_is_ready);
	RESET_FAKE(hw_uart_poll_in);
	RESET_FAKE(hw_uart_poll_out);
	RESET_FAKE(hw_adc_is_ready);
	RESET_FAKE(hw_adc_setup_channel);
	RESET_FAKE(hw_adc_start_async);
	RESET_FAKE(hw_adc_check_async);
	RESET_FAKE(hw_spi_is_ready);
	RESET_FAKE(hw_spi_write);
	FFF_RESET_HISTORY();

	(void)memset(sim_in_levels, 0, sizeof(sim_in_levels));
	(void)memset(sim_out_levels, 0, sizeof(sim_out_levels));
	sim_in_levels[HW_IN_ENTRY_LOWER] = 1;
	sim_in_levels[HW_IN_EXIT_LOWER] = 1;

	fail_out_pin = HW_OUT_COUNT;
	fail_out_Countdown = -1;

	sim_adc_signaled = 1U;
	sim_adc_result = 0;
	sim_adc_raw_lanes[LANE_ENTRY] = 4095;
	sim_adc_raw_lanes[LANE_EXIT] = 4095;
	last_adc_lane = LANE_ENTRY;

	(void)memset(sim_uart_rx_buf, 0, sizeof(sim_uart_rx_buf));
	(void)memset(sim_uart_rx_head, 0, sizeof(sim_uart_rx_head));
	(void)memset(sim_uart_rx_tail, 0, sizeof(sim_uart_rx_tail));
	(void)memset(sim_uart_tx_buf, 0, sizeof(sim_uart_tx_buf));
	(void)memset(sim_uart_tx_len, 0, sizeof(sim_uart_tx_len));

	fail_spi_on_call = -1;
	spi_call_counter = 0;

	hw_out_is_ready_fake.return_val = true;
	hw_in_is_ready_fake.return_val = true;
	hw_uart_is_ready_fake.return_val = true;
	hw_adc_is_ready_fake.return_val = true;
	hw_spi_is_ready_fake.return_val = true;

	hw_in_get_fake.custom_fake = custom_hw_in_get;
	hw_out_set_fake.custom_fake = custom_hw_out_set;
	hw_adc_start_async_fake.custom_fake = custom_hw_adc_start_async;
	hw_adc_check_async_fake.custom_fake = custom_hw_adc_check_async;
	hw_uart_poll_in_fake.custom_fake = custom_hw_uart_poll_in;
	hw_uart_poll_out_fake.custom_fake = custom_hw_uart_poll_out;
	hw_spi_write_fake.custom_fake = custom_hw_spi_write;
}

/* ══════════════════════════════════════════════════════════════════
 * 1. Session & Font tests
 * ══════════════════════════════════════════════════════════════════ */

ZTEST_SUITE(session_font_suite, NULL, NULL, reset_all_fakes, NULL, NULL);

ZTEST(session_font_suite, test_session_lifecycle_and_capacity)
{
	static const char plates[8][8] = {
		"AAA1A00", "BBB2B00", "CCC3C00", "DDD4D00",
		"EEE5E00", "FFF6F00", "GGG7G00", "HHH8H00"
	};

	session_init();
	zassert_equal(session_count(), 0);
	zassert_false(session_is_full());
	zassert_is_null(session_find("AAA1A00"));
	zassert_equal(session_remove("AAA1A00"), -ENOENT);

	for (int i = 0; i < SESSION_MAX_CAPACITY; i++) {
		zassert_ok(session_create(plates[i], (int64_t)(i * 100)));
	}
	zassert_equal(session_count(), SESSION_MAX_CAPACITY);
	zassert_true(session_is_full());

	/* Duplicate and overflow rejected */
	zassert_equal(session_create(plates[0], 500), -EEXIST);
	zassert_equal(session_create("ZZZ9Z99", 500), -ENOSPC);

	const session_t *s = session_find(plates[0]);

	zassert_not_null(s);
	zassert_equal(s->entry_time_ms, 0);

	zassert_ok(session_remove(plates[0]));
	zassert_false(session_has_plate(plates[0]));
	zassert_ok(session_create("ZZZ9Z99", 900));
	zassert_true(session_has_plate("ZZZ9Z99"));
}

ZTEST(session_font_suite, test_font_lookup_bounds)
{
	const uint8_t *space = font_get_char(' ');
	const uint8_t *letter_z = font_get_char('Z');
	const uint8_t *below = font_get_char('\x10');
	const uint8_t *above = font_get_char('~');

	zassert_equal(space[0], 0x00U);
	zassert_not_equal(letter_z[2], 0x00U);
	zassert_equal(below, above);
	zassert_equal(below[2], 0x7EU);
}

/* ══════════════════════════════════════════════════════════════════
 * 2. Shared Async ADC4 tests
 * ══════════════════════════════════════════════════════════════════ */

ZTEST_SUITE(adc_suite, NULL, NULL, reset_all_fakes, NULL, NULL);

ZTEST(adc_suite, test_adc_init_errors)
{
	adc_mgr_t mgr;

	hw_adc_is_ready_fake.return_val = false;
	zassert_equal(adc_mgr_init(&mgr), -ENODEV);
	zassert_true(adc_mgr_is_faulted(&mgr));

	hw_adc_is_ready_fake.return_val = true;
	hw_adc_setup_channel_fake.return_val = -EIO;
	zassert_equal(adc_mgr_init(&mgr), -EIO);
	zassert_true(adc_mgr_is_faulted(&mgr));

	int setup_rets[2] = { 0, -EINVAL };

	RESET_FAKE(hw_adc_setup_channel);
	SET_RETURN_SEQ(hw_adc_setup_channel, setup_rets, 2);
	zassert_equal(adc_mgr_init(&mgr), -EINVAL);
	zassert_true(adc_mgr_is_faulted(&mgr));
}

ZTEST(adc_suite, test_adc_alternation_and_thresholds)
{
	adc_mgr_t mgr;

	zassert_ok(adc_mgr_init(&mgr));
	sim_adc_raw_lanes[LANE_ENTRY] = 500;
	sim_adc_raw_lanes[LANE_EXIT] = 1500;

	/* Submit first read (entry) */
	sim_adc_signaled = 0U;
	zassert_ok(adc_mgr_service(&mgr, 10));
	zassert_true(mgr.pending);
	zassert_equal(mgr.active_lane, LANE_ENTRY);

	/* Still pending before deadline */
	zassert_ok(adc_mgr_service(&mgr, 20));
	zassert_false(adc_mgr_has_fresh(&mgr, LANE_ENTRY, 20));

	/* Complete entry, automatically submits exit */
	sim_adc_signaled = 1U;
	zassert_ok(adc_mgr_service(&mgr, 30));
	zassert_equal(mgr.active_lane, LANE_EXIT);
	zassert_true(adc_mgr_car_present(&mgr, LANE_ENTRY, 30));
	zassert_false(adc_mgr_car_departed(&mgr, LANE_ENTRY, 30));

	/* Complete exit, alternates back to entry */
	zassert_ok(adc_mgr_service(&mgr, 40));
	zassert_equal(mgr.active_lane, LANE_ENTRY);
	zassert_true(adc_mgr_car_departed(&mgr, LANE_EXIT, 40));
	zassert_false(adc_mgr_car_present(&mgr, LANE_EXIT, 40));

	/* Stale sample check (>= 100 ms old) */
	zassert_false(adc_mgr_has_fresh(&mgr, LANE_ENTRY, 130));
}

ZTEST(adc_suite, test_adc_fault_latching_cases)
{
	adc_mgr_t mgr;

	/* Start failure */
	zassert_ok(adc_mgr_init(&mgr));
	hw_adc_start_async_fake.custom_fake = NULL;
	hw_adc_start_async_fake.return_val = -EBUSY;
	zassert_equal(adc_mgr_service(&mgr, 0), -EBUSY);
	zassert_true(adc_mgr_is_faulted(&mgr));
	zassert_equal(adc_mgr_service(&mgr, 5), -EIO);

	/* Deadline >= 100 ms fault */
	hw_adc_start_async_fake.custom_fake = custom_hw_adc_start_async;
	zassert_ok(adc_mgr_init(&mgr));
	sim_adc_signaled = 0U;
	zassert_ok(adc_mgr_service(&mgr, 10));
	zassert_equal(adc_mgr_service(&mgr, 110), -ETIMEDOUT);
	zassert_true(adc_mgr_is_faulted(&mgr));

	/* Driver result != 0 fault */
	zassert_ok(adc_mgr_init(&mgr));
	sim_adc_signaled = 1U;
	sim_adc_result = -EIO;
	zassert_ok(adc_mgr_service(&mgr, 0));
	zassert_equal(adc_mgr_service(&mgr, 5), -ERANGE);
	zassert_true(adc_mgr_is_faulted(&mgr));

	/* Negative raw sample fault */
	sim_adc_result = 0;
	sim_adc_raw_lanes[LANE_ENTRY] = -5;
	zassert_ok(adc_mgr_init(&mgr));
	zassert_ok(adc_mgr_service(&mgr, 0));
	zassert_equal(adc_mgr_service(&mgr, 5), -ERANGE);

	/* Out-of-range > 4095 raw sample fault */
	sim_adc_raw_lanes[LANE_ENTRY] = 5000;
	zassert_ok(adc_mgr_init(&mgr));
	zassert_ok(adc_mgr_service(&mgr, 0));
	zassert_equal(adc_mgr_service(&mgr, 5), -ERANGE);
}

/* ══════════════════════════════════════════════════════════════════
 * 3. Camera UART parser & retry tests
 * ══════════════════════════════════════════════════════════════════ */

ZTEST_SUITE(camera_suite, NULL, NULL, reset_all_fakes, NULL, NULL);

ZTEST(camera_suite, test_camera_plate_validation_and_framing)
{
	camera_ctx_t cam;

	zassert_true(camera_validate_plate("ABC1D23"));
	zassert_true(camera_validate_plate("ABC1234"));
	zassert_false(camera_validate_plate("ABC1D2"));
	zassert_false(camera_validate_plate("@BC1D23"));
	zassert_false(camera_validate_plate("aBC1D23"));
	zassert_false(camera_validate_plate("AB[1D23"));
	zassert_false(camera_validate_plate("ABC/D23"));
	zassert_false(camera_validate_plate("ABCXD23"));
	zassert_false(camera_validate_plate("ABC:D23"));
	zassert_false(camera_validate_plate("ABC1-23"));
	zassert_false(camera_validate_plate("ABC1@23"));
	zassert_false(camera_validate_plate("ABC1[23"));
	zassert_false(camera_validate_plate("ABC1D/3"));
	zassert_false(camera_validate_plate("ABC1D:3"));
	zassert_false(camera_validate_plate("ABC1D2/"));
	zassert_false(camera_validate_plate("ABC1D2:"));

	camera_init(&cam, LANE_EXIT);
	/* Feed while IDLE (ignored cleanly) */
	sim_uart_push_rx(HW_UART_EXIT_CAMERA, "+PLATE:1,\"ABC1D23\",95\r\n");
	camera_service(&cam, 10);
	zassert_equal(cam.state, CAMERA_STATE_IDLE);

	/* Capture ID wraparound at 999999999 */
	cam.next_id = CAMERA_MAX_CAPTURE_ID;
	camera_start(&cam, 20);
	zassert_equal(cam.active_id, CAMERA_MAX_CAPTURE_ID);
	zassert_equal(cam.next_id, 1U);

	/* Malformed frames: bare LF, bare CR, control char, overlong >126 body,
	 * bad fields, leading zero ID/conf, out-of-range ID/conf, wrong ID
	 */
	sim_uart_push_rx(HW_UART_EXIT_CAMERA,
			 "BARE_LF\n"
			 "BARE\rCR\r\n"
			 "BAD\x01" "CHAR\r\n"
			 "BAD\x7f" "CHAR\r\n");
	camera_service(&cam, 25);

	for (size_t i = 0U; i < 130U; i++) {
		camera_rx_byte(&cam, (uint8_t)'X', 26);
	}
	camera_rx_byte(&cam, (uint8_t)'\r', 26);
	camera_rx_byte(&cam, (uint8_t)'\n', 26);

	/* Partial-frame 1000 ms deadline discards suffix through LF */
	camera_rx_byte(&cam, (uint8_t)'+', 30);
	camera_service(&cam, 1030); /* >= 1000 ms partial frame timeout, also retries attempt 2 (id=1) */
	zassert_equal(cam.attempt, 2U);
	zassert_equal(cam.active_id, 1U);
	/* Suffix of timed-out frame discarded, then valid frame resynchronizes */
	sim_uart_push_rx(HW_UART_EXIT_CAMERA,
			 "PLATE:1,\"ABC1D23\",95\r\n"
			 "OK\r\n"
			 "ERROR\r\n"
			 "UNKNOWN8\r\n"
			 "+PLATE:\r\n"
			 "+PLATE:1\r\n"
			 "+PLATE:,\"ABC1D23\",95\r\n"
			 "+PLATE:01,\"ABC1D23\",95\r\n"
			 "+PLATE:0,\"ABC1D23\",95\r\n"
			 "+PLATE:12345678901,\"ABC1D23\",95\r\n"
			 "+PLATE:1/,\"ABC1D23\",95\r\n"
			 "+PLATE:1a,\"ABC1D23\",95\r\n"
			 "+PLATE:9999999990,\"ABC1D23\",95\r\n"
			 "+PLATE:99,\"ABC1D23\",95\r\n"
			 "+PLATE:1,ABC1D23,95\r\n"
			 "+PLATE:1,\"ABC1D23\r\n"
			 "+PLATE:1,\"ABC1D2\",95\r\n"
			 "+PLATE:1,\"abc1d23\",95\r\n"
			 "+PLATE:1,\"ABC1D23\"95\r\n"
			 "+PLATE:1,\"ABC1D23\",09\r\n"
			 "+PLATE:1,\"ABC1D23\",101\r\n");
	for (int s = 0; s < 10; s++) {
		camera_service(&cam, 1040 + s);
	}
	zassert_equal(cam.state, CAMERA_STATE_WAITING);

	/* Low confidence (0) consumes attempt 2 -> attempt 3 (id=2), then low conf fails */
	sim_uart_push_rx(HW_UART_EXIT_CAMERA,
			 "+PLATE:1,\"ABC1D23\",0\r\n"
			 "+PLATE:2,\"ABC1D23\",84\r\n");
	camera_service(&cam, 1050);
	zassert_equal(cam.state, CAMERA_STATE_FAILED);

	/* Timeout exhaustion across 3 attempts */
	camera_start(&cam, 2000);
	camera_service(&cam, 3000);
	camera_service(&cam, 4000);
	camera_service(&cam, 5000);
	zassert_equal(cam.state, CAMERA_STATE_FAILED);
}

/* ══════════════════════════════════════════════════════════════════
 * 4. Payment UART parser & tariff tests
 * ══════════════════════════════════════════════════════════════════ */

ZTEST_SUITE(payment_suite, NULL, NULL, reset_all_fakes, NULL, NULL);

ZTEST(payment_suite, test_tariff_and_payment_protocol_edges)
{
	payment_ctx_t pay;

	zassert_equal(payment_calc_tariff(0, 0), -1);
	zassert_equal(payment_calc_tariff(-10, 1000), 200);
	zassert_equal(payment_calc_tariff(0, 60000), 200);
	zassert_equal(payment_calc_tariff(60000, 60000), 200);
	zassert_equal(payment_calc_tariff(60001, 60000), 300);
	zassert_equal(payment_calc_tariff(9999000, 1000), 1000000);
	zassert_equal(payment_calc_tariff(9999001, 1000), -1);

	payment_init(&pay);
	/* Cancel while no active UUID */
	payment_cancel_active(&pay);
	zassert_equal(pay.state, PAYMENT_STATE_FAILED);

	/* Start charge and test malformed/stale/CR/overlong/partial-deadline frames */
	payment_start_charge(&pay, "ABC1D23", 400U, 100);
	zassert_equal(pay.state, PAYMENT_STATE_WAITING);
	zassert_equal(strlen(pay.active_uuid), 36U);

	/* Empty LF, CR rejection (must NOT authorize!), control char, >127 body */
	char cr_frame[96];

	(void)snprintf(cr_frame, sizeof(cr_frame),
		       "\nP1 RESULT %s PAID 400 BRL\r\n", pay.active_uuid);
	sim_uart_push_rx(HW_UART_PAYMENT, cr_frame);
	payment_service(&pay, 110);
	zassert_equal(pay.state, PAYMENT_STATE_WAITING);

	payment_rx_byte(&pay, 0x7FU, 111);
	payment_rx_byte(&pay, (uint8_t)'\n', 111);
	for (size_t i = 0U; i < 130U; i++) {
		payment_rx_byte(&pay, (uint8_t)'A', 112);
	}
	payment_rx_byte(&pay, (uint8_t)'\n', 112);

	/* Partial frame 1000 ms timeout + retry send #2 */
	payment_rx_byte(&pay, (uint8_t)'P', 120);
	payment_service(&pay, 1120);
	zassert_equal(pay.send_count, 2U);
	payment_rx_byte(&pay, (uint8_t)'\n', 1121);

	/* Invalid fields: short, wrong UUID, missing space, unknown status,
	 * missing BRL, leading-zero amount, non-digit amount, >1000000 amount,
	 * mismatched amount
	 */
	char bad[1024];

	(void)snprintf(bad, sizeof(bad),
		       "P1 ERROR TIMEOUT\n"
		       "P1 UNKNOWN_LONG_HEADER_THAT_EXCEEDS_FIFTY_SEVEN_CHARS_TOTAL_LEN\n"
		       "P1 RESULT 00000000-0000-4000-8000-000000000000 PAID 400 BRL\n"
		       "P1 RESULT %sXPAID 400 BRL\n"
		       "P1 RESULT %s BROKEN 400 BRL\n"
		       "P1 RESULT %s PAID 400 USD\n"
		       "P1 RESULT %s PAID  BRL\n"
		       "P1 RESULT %s PAID 0400 BRL\n"
		       "P1 RESULT %s PAID 4/0 BRL\n"
		       "P1 RESULT %s PAID 40a BRL\n"
		       "P1 RESULT %s PAID 12345678 BRL\n"
		       "P1 RESULT %s PAID 2000000 BRL\n"
		       "P1 RESULT %s PAID 300 BRL\n",
		       pay.active_uuid, pay.active_uuid, pay.active_uuid,
		       pay.active_uuid, pay.active_uuid, pay.active_uuid,
		       pay.active_uuid, pay.active_uuid, pay.active_uuid,
		       pay.active_uuid);
	sim_uart_push_rx(HW_UART_PAYMENT, bad);
	for (int step = 0; step < 16; step++) {
		payment_service(&pay, 1130 + step);
	}
	zassert_equal(pay.state, PAYMENT_STATE_WAITING);

	/* Valid CANCELLED response fails the charge */
	char cancelled[96];

	(void)snprintf(cancelled, sizeof(cancelled),
		       "P1 RESULT %s CANCELLED 400 BRL\n", pay.active_uuid);
	sim_uart_push_rx(HW_UART_PAYMENT, cancelled);
	payment_service(&pay, 1150);
	payment_service(&pay, 1151);
	zassert_equal(pay.state, PAYMENT_STATE_FAILED);

	/* Exhaust all 3 sends on timeout -> cancels active UUID */
	payment_start_charge(&pay, "ABC1D23", 200U, 2000);
	payment_service(&pay, 3000); /* send 2 */
	payment_service(&pay, 4000); /* send 3 */
	payment_service(&pay, 5000); /* exhausted -> CANCEL */
	zassert_equal(pay.state, PAYMENT_STATE_FAILED);
	zassert_equal(pay.active_uuid[0], '\0');
}

/* ══════════════════════════════════════════════════════════════════
 * 5. Non-blocking ST7789V Display tests
 * ══════════════════════════════════════════════════════════════════ */

ZTEST_SUITE(display_suite, NULL, NULL, reset_all_fakes, NULL, NULL);

ZTEST(display_suite, test_display_init_and_error_paths)
{
	display_ctx_t disp;

	/* Readiness and configure failures */
	hw_spi_is_ready_fake.return_val = false;
	zassert_equal(display_start_init(&disp, 0), -ENODEV);
	zassert_true(display_has_error(&disp));
	zassert_equal(display_render_plate(&disp, "ABC1D23"), -EBUSY);
	zassert_equal(display_render_charge_details(&disp, 200U), -EBUSY);

	reset_all_fakes(NULL);
	hw_out_is_ready_fake.return_val = false;
	zassert_equal(display_start_init(&disp, 0), -ENODEV);

	reset_all_fakes(NULL);
	bool dc_rdy[2] = { true, false };

	SET_RETURN_SEQ(hw_out_is_ready, dc_rdy, 2);
	zassert_equal(display_start_init(&disp, 0), -ENODEV);

	reset_all_fakes(NULL);
	hw_out_configure_inactive_fake.return_val = -EIO;
	zassert_equal(display_start_init(&disp, 0), -EIO);

	reset_all_fakes(NULL);
	int cfg_seq[2] = { 0, -EIO };

	SET_RETURN_SEQ(hw_out_configure_inactive, cfg_seq, 2);
	zassert_equal(display_start_init(&disp, 0), -EIO);

	reset_all_fakes(NULL);
	fail_out_pin = HW_OUT_DISPLAY_RESET;
	fail_out_Countdown = 0;
	zassert_equal(display_start_init(&disp, 0), -EIO);

	/* Fail at reset release */
	reset_all_fakes(NULL);
	zassert_ok(display_start_init(&disp, 0));
	display_service(&disp, 5); /* before 10 ms */
	fail_out_pin = HW_OUT_DISPLAY_RESET;
	fail_out_Countdown = 0;
	display_service(&disp, 10);
	zassert_true(display_has_error(&disp));
	display_service(&disp, 20); /* service in error state returns immediately */

	/* Fail at each SPI init step (calls 1..7) and DC set failure */
	for (int call_fail = 1; call_fail <= 7; call_fail++) {
		reset_all_fakes(NULL);
		fail_spi_on_call = call_fail;
		zassert_ok(display_start_init(&disp, 0));
		display_service(&disp, 10);
		display_service(&disp, 125); /* < 120 ms wake */
		display_service(&disp, 130); /* SWRESET */
		display_service(&disp, 270); /* < 150 ms */
		display_service(&disp, 280); /* SLPOUT */
		display_service(&disp, 390); /* < 120 ms */
		display_service(&disp, 400); /* COLMOD + MADCTL + DISPON */
		zassert_true(display_has_error(&disp));
	}

	/* DC pin failure during write_cmd and write_data */
	reset_all_fakes(NULL);
	zassert_ok(display_start_init(&disp, 0));
	display_service(&disp, 10);
	fail_out_pin = HW_OUT_DISPLAY_DC;
	fail_out_Countdown = 0;
	display_service(&disp, 130);
	zassert_true(display_has_error(&disp));

	reset_all_fakes(NULL);
	zassert_ok(display_start_init(&disp, 0));
	display_service(&disp, 10);
	display_service(&disp, 130);
	display_service(&disp, 280);
	fail_out_pin = HW_OUT_DISPLAY_DC;
	fail_out_Countdown = 1; /* allow COLMOD cmd, fail COLMOD data */
	display_service(&disp, 400);
	zassert_true(display_has_error(&disp));

	/* Full successful init + render + render failures at each band/window step */
	reset_all_fakes(NULL);
	zassert_ok(display_start_init(&disp, 0));
	display_service(&disp, 10);
	display_service(&disp, 130);
	display_service(&disp, 280);
	display_service(&disp, 400);
	zassert_true(display_is_ready(&disp));
	zassert_ok(display_render_plate(&disp, "ABC1D23"));
	zassert_ok(display_render_charge_details(&disp, 450U));

	/* Test window/pixel SPI errors inside render_plate and render_charge_details */
	for (int offset = 1; offset <= 6; offset++) {
		reset_all_fakes(NULL);
		zassert_ok(display_start_init(&disp, 0));
		display_service(&disp, 10);
		display_service(&disp, 130);
		display_service(&disp, 280);
		display_service(&disp, 400);
		fail_spi_on_call = spi_call_counter + offset;
		zassert_not_equal(display_render_plate(&disp, "VERYLONGSTRINGOVER240PX"), 0);
	}

	int band_fail_offsets[3] = { 1, 40, 65 };

	for (int b = 0; b < 3; b++) {
		reset_all_fakes(NULL);
		zassert_ok(display_start_init(&disp, 0));
		display_service(&disp, 10);
		display_service(&disp, 130);
		display_service(&disp, 280);
		display_service(&disp, 400);
		fail_spi_on_call = spi_call_counter + band_fail_offsets[b];
		zassert_not_equal(display_render_charge_details(&disp, 200U), 0);
	}
}

/* ══════════════════════════════════════════════════════════════════
 * 6. Full Controller & Lane State Machine tests
 * ══════════════════════════════════════════════════════════════════ */

static void step_to(int64_t *cur_ms, int64_t target_ms)
{
	while (*cur_ms < target_ms) {
		int64_t next = *cur_ms + 5;

		if (next > target_ms) {
			next = target_ms;
		}
		*cur_ms = next;
		hw_uptime_ms_fake.return_val = *cur_ms;
		controller_step(*cur_ms);
	}
}

ZTEST_SUITE(controller_suite, NULL, NULL, reset_all_fakes, NULL, NULL);

ZTEST(controller_suite, test_full_entry_and_paid_exit_cycle)
{
	int64_t now = 0;

	controller_init();
	const controller_t *st = controller_get_state();

	/* Advance past 20 ms settle and ~420 ms display init */
	controller_step(0);
	step_to(&now, 430);
	zassert_equal(st->lanes[LANE_ENTRY].state, LANE_STATE_IDLE);
	zassert_equal(st->lanes[LANE_EXIT].state, LANE_STATE_IDLE);
	zassert_true(display_is_ready(&st->display));

	/* Entry vehicle approaches (< 683) */
	sim_adc_raw_lanes[LANE_ENTRY] = 500;
	step_to(&now, 445);
	zassert_equal(st->lanes[LANE_ENTRY].state, LANE_STATE_IDENTIFYING);

	/* Camera accepts ABC1D23 */
	sim_uart_push_rx(HW_UART_ENTRY_CAMERA, "OK\r\n+PLATE:1,\"ABC1D23\",93\r\n");
	step_to(&now, 455);
	zassert_equal(st->lanes[LANE_ENTRY].state, LANE_STATE_OPENING);
	zassert_true(sim_out_levels[HW_OUT_ENTRY_MOTOR_EN]);
	zassert_true(sim_out_levels[HW_OUT_ENTRY_MOTOR_DIR]);

	/* Upper limit via deferred EXTI work stops motor, then green turns on */
	sim_in_levels[HW_IN_ENTRY_LOWER] = 0;
	sim_in_levels[HW_IN_ENTRY_UPPER] = 1;
	controller_exti_work();
	zassert_false(sim_out_levels[HW_OUT_ENTRY_MOTOR_EN]);
	step_to(&now, 500);
	zassert_equal(st->lanes[LANE_ENTRY].state, LANE_STATE_OPEN);
	zassert_true(sim_out_levels[HW_OUT_ENTRY_GREEN]);

	/* Beam active -> clear + departed -> closes and creates session */
	sim_in_levels[HW_IN_ENTRY_BEAM] = 1;
	step_to(&now, 540);
	sim_in_levels[HW_IN_ENTRY_BEAM] = 0;
	sim_adc_raw_lanes[LANE_ENTRY] = 2000;
	step_to(&now, 580);
	zassert_equal(st->lanes[LANE_ENTRY].state, LANE_STATE_CLOSING);
	zassert_equal(session_count(), 1);

	/* Lower limit stops close -> IDLE */
	sim_in_levels[HW_IN_ENTRY_UPPER] = 0;
	sim_in_levels[HW_IN_ENTRY_LOWER] = 1;
	controller_exti_work();
	step_to(&now, 620);
	zassert_equal(st->lanes[LANE_ENTRY].state, LANE_STATE_IDLE);

	/* Exit vehicle approaches (< 683) */
	sim_adc_raw_lanes[LANE_EXIT] = 500;
	step_to(&now, 700);
	zassert_equal(st->lanes[LANE_EXIT].state, LANE_STATE_IDENTIFYING);

	sim_uart_push_rx(HW_UART_EXIT_CAMERA, "OK\r\n+PLATE:1,\"ABC1D23\",90\r\n");
	step_to(&now, 710);
	zassert_equal(st->lanes[LANE_EXIT].state, LANE_STATE_PAYING);

	/* Reply PAID with exact active UUID and amount */
	char paid_msg[128];

	(void)snprintf(paid_msg, sizeof(paid_msg),
		       "P1 RESULT %s PAID %" PRIu32 " BRL\n",
		       st->lanes[LANE_EXIT].payment.active_uuid,
		       st->lanes[LANE_EXIT].payment.amount_centavos);
	sim_uart_push_rx(HW_UART_PAYMENT, paid_msg);
	step_to(&now, 720);
	zassert_equal(st->lanes[LANE_EXIT].state, LANE_STATE_OPENING);

	/* Complete exit opening, passage, and closing -> session removed */
	sim_in_levels[HW_IN_EXIT_LOWER] = 0;
	sim_in_levels[HW_IN_EXIT_UPPER] = 1;
	controller_exti_work();
	step_to(&now, 750);
	zassert_equal(st->lanes[LANE_EXIT].state, LANE_STATE_OPEN);

	sim_in_levels[HW_IN_EXIT_BEAM] = 1;
	step_to(&now, 780);
	sim_in_levels[HW_IN_EXIT_BEAM] = 0;
	sim_adc_raw_lanes[LANE_EXIT] = 2000;
	step_to(&now, 810);
	zassert_equal(session_count(), 0);

	sim_in_levels[HW_IN_EXIT_UPPER] = 0;
	sim_in_levels[HW_IN_EXIT_LOWER] = 1;
	controller_exti_work();
	step_to(&now, 840);
	zassert_equal(st->lanes[LANE_EXIT].state, LANE_STATE_IDLE);
}

ZTEST(controller_suite, test_lane_faults_safety_and_driver_errors)
{
	/* 1. Hardware setup error branches in lane_hw_setup / controller_init */
	hw_out_is_ready_fake.return_val = false;
	zassert_equal(lane_hw_setup(LANE_ENTRY), -ENODEV);

	reset_all_fakes(NULL);
	hw_out_configure_inactive_fake.return_val = -EIO;
	zassert_equal(lane_hw_setup(LANE_ENTRY), -EIO);

	reset_all_fakes(NULL);
	fail_out_pin = HW_OUT_ENTRY_RED;
	fail_out_Countdown = 0;
	zassert_equal(lane_hw_setup(LANE_ENTRY), -EIO);

	reset_all_fakes(NULL);
	hw_in_is_ready_fake.return_val = false;
	zassert_equal(lane_hw_setup(LANE_ENTRY), -ENODEV);

	reset_all_fakes(NULL);
	hw_in_configure_fake.return_val = -EIO;
	zassert_equal(lane_hw_setup(LANE_ENTRY), -EIO);

	reset_all_fakes(NULL);
	hw_in_register_callback_fake.return_val = -EIO;
	zassert_equal(lane_hw_setup(LANE_ENTRY), -EIO);

	reset_all_fakes(NULL);
	hw_in_configure_interrupt_fake.return_val = -EIO;
	zassert_equal(lane_hw_setup(LANE_ENTRY), -EIO);

	reset_all_fakes(NULL);
	hw_uart_is_ready_fake.return_val = false;
	zassert_equal(lane_hw_setup(LANE_ENTRY), -ENODEV);

	reset_all_fakes(NULL);
	bool uart_seq[2] = { true, false };

	SET_RETURN_SEQ(hw_uart_is_ready, uart_seq, 2);
	zassert_equal(lane_hw_setup(LANE_EXIT), -ENODEV);

	/* controller_init latches io_fault when lane_hw_setup fails */
	reset_all_fakes(NULL);
	hw_uart_is_ready_fake.return_val = false;
	controller_init();
	controller_step(25);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_LATCHED);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_LATCHED);

	/* 2. Partial startup -> unrecoverable assistance, buzzer expires after 100 ms */
	reset_all_fakes(NULL);
	sim_in_levels[HW_IN_ENTRY_LOWER] = 0;
	int64_t tnow = 0;

	controller_init();
	controller_step(0);
	step_to(&tnow, 25);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_ASSISTANCE);
	zassert_true(sim_out_levels[HW_OUT_ENTRY_BUZZER]);
	step_to(&tnow, 130);
	zassert_false(sim_out_levels[HW_OUT_ENTRY_BUZZER]);

	/* 3. Contradictory limits & IDLE missing lower */
	reset_all_fakes(NULL);
	controller_init();
	controller_step(25);
	sim_in_levels[HW_IN_ENTRY_LOWER] = 0;
	controller_step(30);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_ASSISTANCE);
	sim_in_levels[HW_IN_EXIT_UPPER] = 1;
	sim_in_levels[HW_IN_EXIT_LOWER] = 1;
	controller_step(35);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_LATCHED);
	controller_step(40); /* stays latched */

	/* 4. Entry duplicate/full, camera fail, ID abandon, rearm */
	reset_all_fakes(NULL);
	controller_init();
	(void)session_create("DUP1A23", 0);
	for (int64_t t = 0; t <= 30; t += 5) {
		controller_step(t);
	}
	/* Abandon during identifying */
	sim_adc_raw_lanes[LANE_ENTRY] = 500;
	controller_step(35);
	controller_step(40);
	sim_adc_raw_lanes[LANE_ENTRY] = 2000;
	controller_step(45);
	controller_step(50);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_IDLE);

	/* Duplicate session -> closed-barrier assistance -> rearms on departure */
	sim_adc_raw_lanes[LANE_ENTRY] = 500;
	controller_step(55);
	controller_step(60);
	sim_uart_push_rx(HW_UART_ENTRY_CAMERA, "+PLATE:2,\"DUP1A23\",95\r\n");
	controller_step(65);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_ASSISTANCE);
	sim_adc_raw_lanes[LANE_ENTRY] = 2000;
	controller_step(70);
	controller_step(75);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_IDLE);

	/* 5. Opening timeout, passage timeout, obstruction during close */
	sim_adc_raw_lanes[LANE_ENTRY] = 500;
	controller_step(80);
	controller_step(85);
	sim_uart_push_rx(HW_UART_ENTRY_CAMERA, "+PLATE:3,\"NEW1B23\",95\r\n");
	controller_step(90);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_OPENING);
	tnow = 90;
	step_to(&tnow, 2600); /* 2500 ms motor budget exceeded */
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_ASSISTANCE);

	/* Passage timeout & obstruction during close */
	reset_all_fakes(NULL);
	controller_init();
	for (int64_t t = 0; t <= 30; t += 5) {
		controller_step(t);
	}
	sim_adc_raw_lanes[LANE_ENTRY] = 500;
	controller_step(35);
	controller_step(40);
	sim_uart_push_rx(HW_UART_ENTRY_CAMERA, "+PLATE:1,\"NEW1B23\",95\r\n");
	controller_step(45);
	sim_in_levels[HW_IN_ENTRY_LOWER] = 0;
	sim_in_levels[HW_IN_ENTRY_UPPER] = 1;
	sim_in_levels[HW_IN_ENTRY_BEAM] = 1; /* beam already active before green */
	tnow = 45;
	step_to(&tnow, 100);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_OPEN);
	sim_in_levels[HW_IN_ENTRY_BEAM] = 0;
	controller_step(110);
	sim_in_levels[HW_IN_ENTRY_BEAM] = 1;
	controller_step(120);
	sim_in_levels[HW_IN_ENTRY_BEAM] = 0;
	sim_adc_raw_lanes[LANE_ENTRY] = 2000;
	controller_step(130);
	controller_step(135);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_CLOSING);
	/* Obstruction during close stops motor immediately */
	sim_in_levels[HW_IN_ENTRY_UPPER] = 0;
	sim_in_levels[HW_IN_ENTRY_BEAM] = 1;
	controller_exti_work();
	zassert_false(sim_out_levels[HW_OUT_ENTRY_MOTOR_EN]);
	controller_step(140);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_ASSISTANCE);

	/* 6. Exit lane edge paths: unknown plate, prepare_charge wait/abandon/overflow,
	 * payment abandon/fail, closing timeout, negative GPIO read
	 */
	reset_all_fakes(NULL);
	controller_init();
	/* Keep display unready (<400 ms) while exit car arrives with active session */
	(void)session_create("EXT1A00", 0);
	controller_step(25);
	sim_adc_raw_lanes[LANE_EXIT] = 500;
	controller_step(30);
	controller_step(35);
	/* Unknown plate first -> assistance -> rearm */
	sim_uart_push_rx(HW_UART_EXIT_CAMERA, "+PLATE:1,\"UNK9Z99\",95\r\n");
	controller_step(40);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_ASSISTANCE);
	sim_adc_raw_lanes[LANE_EXIT] = 2000;
	controller_step(45);
	controller_step(50);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_IDLE);

	/* Known plate while display still initializing -> PREPARE_CHARGE -> depart */
	sim_adc_raw_lanes[LANE_EXIT] = 500;
	controller_step(55);
	controller_step(60);
	sim_uart_push_rx(HW_UART_EXIT_CAMERA, "+PLATE:2,\"EXT1A00\",95\r\n");
	controller_step(65);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_PREPARE_CHARGE);
	sim_adc_raw_lanes[LANE_EXIT] = 2000;
	controller_step(70);
	controller_step(75);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_IDLE);

	/* Known plate with >9999 units tariff overflow -> assistance.
	 * hw_uptime_ms stays equal to the step time: the display yields to the
	 * ADC with that clock, and a jumped uptime would latch a false fault.
	 */
	(void)session_remove("EXT1A00");
	(void)session_create("EXT1A00", -700000000LL);
	for (int64_t t = 80; t <= 450; t += 10) {
		hw_uptime_ms_fake.return_val = t;
		controller_step(t);
	}
	sim_adc_raw_lanes[LANE_EXIT] = 500;
	hw_uptime_ms_fake.return_val = 455;
	controller_step(455);
	hw_uptime_ms_fake.return_val = 460;
	controller_step(460);
	sim_uart_push_rx(HW_UART_EXIT_CAMERA, "+PLATE:3,\"EXT1A00\",95\r\n");
	hw_uptime_ms_fake.return_val = 465;
	controller_step(465);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_ASSISTANCE);
	sim_adc_raw_lanes[LANE_EXIT] = 2000;
	hw_uptime_ms_fake.return_val = 470;
	controller_step(470);
	hw_uptime_ms_fake.return_val = 475;
	controller_step(475);

	/* Payment departure abandons & sends CANCEL */
	(void)session_remove("EXT1A00");
	(void)session_create("EXT1A00", 0);
	hw_uptime_ms_fake.return_val = 480;
	sim_adc_raw_lanes[LANE_EXIT] = 500;
	controller_step(480);
	hw_uptime_ms_fake.return_val = 485;
	controller_step(485);
	sim_uart_push_rx(HW_UART_EXIT_CAMERA, "+PLATE:4,\"EXT1A00\",95\r\n");
	hw_uptime_ms_fake.return_val = 490;
	controller_step(490);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_PAYING);
	sim_adc_raw_lanes[LANE_EXIT] = 2000;
	hw_uptime_ms_fake.return_val = 495;
	controller_step(495);
	hw_uptime_ms_fake.return_val = 500;
	controller_step(500);
	zassert_not_null(strstr(sim_uart_tx_buf[HW_UART_PAYMENT], "P1 CANCEL "));

	/* Negative GPIO input fault latches lane */
	sim_in_levels[HW_IN_EXIT_LOWER] = -EIO;
	hw_uptime_ms_fake.return_val = 505;
	controller_step(505);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_LATCHED);
}

ZTEST(controller_suite, test_gpio_output_and_remaining_edge_branches)
{
	int64_t now = 0;

	/* 1. Missing lower in IDENTIFYING, PREPARE_CHARGE, PAYING */
	controller_init();
	(void)session_create("EDG1A00", 0);
	controller_step(0);
	step_to(&now, 25);
	sim_adc_raw_lanes[LANE_ENTRY] = 500;
	step_to(&now, 35);
	sim_in_levels[HW_IN_ENTRY_LOWER] = 0;
	step_to(&now, 40);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_ASSISTANCE);

	sim_adc_raw_lanes[LANE_EXIT] = 500;
	step_to(&now, 50);
	sim_uart_push_rx(HW_UART_EXIT_CAMERA, "+PLATE:1,\"EDG1A00\",95\r\n");
	step_to(&now, 55);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_PREPARE_CHARGE);
	sim_in_levels[HW_IN_EXIT_LOWER] = 0;
	step_to(&now, 60);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_ASSISTANCE);

	/* Missing lower during PAYING + display errors in PREPARE_CHARGE */
	reset_all_fakes(NULL);
	now = 0;
	controller_init();
	(void)session_create("EDG1A00", 0);
	controller_step(0);
	step_to(&now, 430);
	sim_adc_raw_lanes[LANE_EXIT] = 500;
	step_to(&now, 445);
	fail_spi_on_call = spi_call_counter + 1; /* fail render_plate */
	sim_uart_push_rx(HW_UART_EXIT_CAMERA, "+PLATE:1,\"EDG1A00\",95\r\n");
	step_to(&now, 455);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_ASSISTANCE);

	/* Rearm while display has_error -> enters assistance via display_has_error */
	sim_adc_raw_lanes[LANE_EXIT] = 2000;
	step_to(&now, 470);
	sim_adc_raw_lanes[LANE_EXIT] = 500;
	step_to(&now, 485);
	sim_uart_push_rx(HW_UART_EXIT_CAMERA, "+PLATE:2,\"EDG1A00\",95\r\n");
	step_to(&now, 495);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_ASSISTANCE);

	/* Fail render_charge_details */
	reset_all_fakes(NULL);
	now = 0;
	controller_init();
	(void)session_create("EDG1A00", 0);
	controller_step(0);
	step_to(&now, 430);
	sim_adc_raw_lanes[LANE_EXIT] = 500;
	step_to(&now, 445);
	fail_spi_on_call = spi_call_counter + 40; /* allow plate, fail details */
	sim_uart_push_rx(HW_UART_EXIT_CAMERA, "+PLATE:1,\"EDG1A00\",95\r\n");
	step_to(&now, 455);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_ASSISTANCE);

	/* Missing lower during PAYING */
	reset_all_fakes(NULL);
	now = 0;
	controller_init();
	(void)session_create("EDG1A00", 0);
	controller_step(0);
	step_to(&now, 430);
	sim_adc_raw_lanes[LANE_EXIT] = 500;
	step_to(&now, 445);
	sim_uart_push_rx(HW_UART_EXIT_CAMERA, "+PLATE:1,\"EDG1A00\",95\r\n");
	step_to(&now, 455);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_PAYING);
	sim_in_levels[HW_IN_EXIT_LOWER] = 0;
	step_to(&now, 465);
	zassert_equal(controller_get_state()->lanes[LANE_EXIT].state,
		      LANE_STATE_ASSISTANCE);

	/* 2. GPIO output error paths (motor_dir, motor_en, green, red, buzzer) */
	reset_all_fakes(NULL);
	now = 0;
	controller_init();
	controller_step(0);
	fail_out_pin = HW_OUT_ENTRY_RED;
	fail_out_Countdown = 0;
	step_to(&now, 25); /* set_red_only fails -> io_fault -> LATCHED */
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_LATCHED);

	reset_all_fakes(NULL);
	now = 0;
	sim_in_levels[HW_IN_ENTRY_LOWER] = 0;
	controller_init();
	controller_step(0);
	fail_out_pin = HW_OUT_ENTRY_BUZZER;
	fail_out_Countdown = 0;
	step_to(&now, 25); /* start_buzzer_pulse fails -> io_fault -> LATCHED */
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_LATCHED);

	reset_all_fakes(NULL);
	now = 0;
	sim_in_levels[HW_IN_ENTRY_LOWER] = 0;
	controller_init();
	controller_step(0);
	step_to(&now, 25); /* buzzer starts OK */
	fail_out_pin = HW_OUT_ENTRY_BUZZER;
	fail_out_Countdown = 0;
	step_to(&now, 130); /* service_buzzer off fails -> io_fault -> LATCHED */
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_LATCHED);

	/* start_motor_open failure */
	reset_all_fakes(NULL);
	now = 0;
	controller_init();
	controller_step(0);
	step_to(&now, 25);
	sim_adc_raw_lanes[LANE_ENTRY] = 500;
	step_to(&now, 40);
	fail_out_pin = HW_OUT_ENTRY_MOTOR_DIR;
	fail_out_Countdown = 0;
	sim_uart_push_rx(HW_UART_ENTRY_CAMERA, "+PLATE:1,\"ABC1D23\",95\r\n");
	step_to(&now, 50);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_LATCHED);

	/* set_green_only failure, passage timeout, and transition_open_to_closing failure */
	reset_all_fakes(NULL);
	now = 0;
	controller_init();
	controller_step(0);
	step_to(&now, 25);
	sim_adc_raw_lanes[LANE_ENTRY] = 500;
	step_to(&now, 40);
	sim_uart_push_rx(HW_UART_ENTRY_CAMERA, "+PLATE:1,\"ABC1D23\",95\r\n");
	step_to(&now, 50);
	sim_in_levels[HW_IN_ENTRY_LOWER] = 0;
	sim_in_levels[HW_IN_ENTRY_UPPER] = 1;
	fail_out_pin = HW_OUT_ENTRY_GREEN;
	fail_out_Countdown = 0;
	step_to(&now, 60);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_LATCHED);

	/* Passage 10000 ms timeout and closing motor_en failure */
	reset_all_fakes(NULL);
	now = 0;
	controller_init();
	controller_step(0);
	step_to(&now, 25);
	sim_adc_raw_lanes[LANE_ENTRY] = 500;
	step_to(&now, 40);
	sim_uart_push_rx(HW_UART_ENTRY_CAMERA, "+PLATE:1,\"ABC1D23\",95\r\n");
	step_to(&now, 50);
	sim_in_levels[HW_IN_ENTRY_LOWER] = 0;
	sim_in_levels[HW_IN_ENTRY_UPPER] = 1;
	step_to(&now, 60);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_OPEN);
	step_to(&now, 10070); /* passage timeout -> assistance */
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_ASSISTANCE);

	/* Closing timeout and stop_motor_locked failure */
	reset_all_fakes(NULL);
	now = 0;
	controller_init();
	controller_step(0);
	step_to(&now, 25);
	sim_adc_raw_lanes[LANE_ENTRY] = 500;
	step_to(&now, 40);
	sim_uart_push_rx(HW_UART_ENTRY_CAMERA, "+PLATE:1,\"ABC1D23\",95\r\n");
	step_to(&now, 50);
	sim_in_levels[HW_IN_ENTRY_LOWER] = 0;
	sim_in_levels[HW_IN_ENTRY_UPPER] = 1;
	step_to(&now, 60);
	sim_in_levels[HW_IN_ENTRY_BEAM] = 1;
	step_to(&now, 70);
	sim_in_levels[HW_IN_ENTRY_BEAM] = 0;
	sim_adc_raw_lanes[LANE_ENTRY] = 2000;
	step_to(&now, 85);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_CLOSING);
	sim_in_levels[HW_IN_ENTRY_UPPER] = 0;
	fail_out_pin = HW_OUT_ENTRY_MOTOR_EN;
	fail_out_Countdown = 0;
	step_to(&now, 2600); /* closing timeout + stop_motor_locked error -> LATCHED */
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_LATCHED);

	/* 3. Lot full on entry + transition_open_to_closing output failures */
	reset_all_fakes(NULL);
	now = 0;
	controller_init();
	(void)session_create("S001A01", 0);
	(void)session_create("S002A02", 0);
	(void)session_create("S003A03", 0);
	(void)session_create("S004A04", 0);
	controller_step(0);
	step_to(&now, 25);
	sim_adc_raw_lanes[LANE_ENTRY] = 500;
	step_to(&now, 40);
	sim_uart_push_rx(HW_UART_ENTRY_CAMERA, "+PLATE:1,\"FUL5E00\",95\r\n");
	step_to(&now, 50);
	zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
		      LANE_STATE_ASSISTANCE);

	/* transition_open_to_closing output error on each of the 4 pins */
	hw_out_pin_t close_pins[4] = {
		HW_OUT_ENTRY_GREEN, HW_OUT_ENTRY_RED,
		HW_OUT_ENTRY_MOTOR_DIR, HW_OUT_ENTRY_MOTOR_EN
	};

	for (int p = 0; p < 4; p++) {
		reset_all_fakes(NULL);
		now = 0;
		controller_init();
		controller_step(0);
		step_to(&now, 25);
		sim_adc_raw_lanes[LANE_ENTRY] = 500;
		step_to(&now, 40);
		sim_uart_push_rx(HW_UART_ENTRY_CAMERA, "+PLATE:1,\"ABC1D23\",95\r\n");
		step_to(&now, 50);
		sim_in_levels[HW_IN_ENTRY_LOWER] = 0;
		sim_in_levels[HW_IN_ENTRY_UPPER] = 1;
		step_to(&now, 60);
		sim_in_levels[HW_IN_ENTRY_BEAM] = 1;
		step_to(&now, 70);
		sim_in_levels[HW_IN_ENTRY_BEAM] = 0;
		sim_adc_raw_lanes[LANE_ENTRY] = 2000;
		fail_out_pin = close_pins[p];
		fail_out_Countdown = 0;
		step_to(&now, 80);
		zassert_equal(controller_get_state()->lanes[LANE_ENTRY].state,
			      LANE_STATE_LATCHED);
	}
}
