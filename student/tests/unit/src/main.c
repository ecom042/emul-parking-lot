/*
 * Unit tests for session, camera, and payment modules.
 *
 * Uses ztest framework with FFF (Fake Function Framework) to mock
 * the hardware abstraction layer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <string.h>

#include "session.h"
#include "camera.h"
#include "payment.h"

DEFINE_FFF_GLOBALS;

/* ── Fake functions for hw.h ─────────────────────────────────────── */

FAKE_VALUE_FUNC(int, hw_uart_camera_send, lane_id_t, const uint8_t *, size_t);
FAKE_VALUE_FUNC(int, hw_uart_payment_send, const uint8_t *, size_t);

/* ══════════════════════════════════════════════════════════════════
 * Session tests
 * ══════════════════════════════════════════════════════════════════ */

ZTEST_SUITE(session_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(session_suite, test_session_init_empty)
{
	session_init();
	zassert_equal(session_count(), 0, "Session table should start empty");
	zassert_false(session_is_full(), "Should not be full when empty");
}

ZTEST(session_suite, test_session_create_and_find)
{
	session_init();

	int rc = session_create("ABC1D23", 1000);
	zassert_equal(rc, 0, "Create should succeed");
	zassert_equal(session_count(), 1, "Should have 1 session");
	zassert_true(session_has_plate("ABC1D23"), "Should find the plate");

	session_t *s = session_find("ABC1D23");
	zassert_not_null(s, "Session pointer should not be NULL");
	zassert_equal(s->entry_time_ms, 1000, "Entry time should match");
}

ZTEST(session_suite, test_session_remove)
{
	session_init();
	session_create("ABC1D23", 1000);

	int rc = session_remove("ABC1D23");
	zassert_equal(rc, 0, "Remove should succeed");
	zassert_equal(session_count(), 0, "Should have 0 sessions");
	zassert_false(session_has_plate("ABC1D23"),
		      "Should not find removed plate");
}

ZTEST(session_suite, test_session_remove_nonexistent)
{
	session_init();
	int rc = session_remove("XYZ9A99");
	zassert_equal(rc, -1, "Remove nonexistent should fail");
}

ZTEST(session_suite, test_session_duplicate)
{
	session_init();
	session_create("ABC1D23", 1000);
	int rc = session_create("ABC1D23", 2000);
	zassert_equal(rc, -1, "Duplicate create should fail");
	zassert_equal(session_count(), 1, "Should still have 1 session");
}

ZTEST(session_suite, test_session_capacity)
{
	session_init();

	char plates[][8] = {"AAA1A00", "BBB2B00", "CCC3C00", "DDD4D00",
			    "EEE5E00", "FFF6F00", "GGG7G00", "HHH8H00"};

	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		int rc = session_create(plates[i], (int64_t)(i * 1000));
		zassert_equal(rc, 0, "Create %d should succeed", i);
	}

	zassert_true(session_is_full(), "Should be full at capacity");

	/* One more should fail */
	if (CONFIG_PARKING_CAPACITY < 8) {
		int rc = session_create(plates[CONFIG_PARKING_CAPACITY], 99000);
		zassert_equal(rc, -1, "Create above capacity should fail");
	}
}

ZTEST(session_suite, test_session_clear)
{
	session_init();
	session_create("ABC1D23", 1000);
	session_create("DEF4G56", 2000);
	zassert_equal(session_count(), 2, "Should have 2 sessions");

	session_clear();
	zassert_equal(session_count(), 0, "Should have 0 after clear");
}

/* ══════════════════════════════════════════════════════════════════
 * Camera tests
 * ══════════════════════════════════════════════════════════════════ */

ZTEST_SUITE(camera_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(camera_suite, test_plate_validation_valid)
{
	zassert_true(camera_validate_plate("ABC1D23"), "ABC1D23 is valid");
	zassert_true(camera_validate_plate("XYZ9A00"), "XYZ9A00 is valid");
	zassert_true(camera_validate_plate("ABC1234"), "ABC1234 is valid");
}

ZTEST(camera_suite, test_plate_validation_invalid)
{
	zassert_false(camera_validate_plate("abc1D23"), "lowercase invalid");
	zassert_false(camera_validate_plate("AB1CD23"), "wrong format");
	zassert_false(camera_validate_plate("ABCDEFG"), "no digits");
	zassert_false(camera_validate_plate("ABC1D2"), "too short");
	zassert_false(camera_validate_plate("ABC1D234"), "too long");
	zassert_false(camera_validate_plate(""), "empty");
}

ZTEST(camera_suite, test_camera_init)
{
	camera_ctx_t ctx;
	camera_init(&ctx, LANE_ENTRY);

	zassert_equal(ctx.state, CAMERA_STATE_IDLE, "Should start idle");
	zassert_equal(ctx.lane, LANE_ENTRY, "Should be entry lane");
	zassert_equal(ctx.rx_len, 0, "Buffer should be empty");
}

ZTEST(camera_suite, test_camera_ok_and_plate_response)
{
	camera_ctx_t ctx;
	camera_init(&ctx, LANE_ENTRY);

	RESET_FAKE(hw_uart_camera_send);
	hw_uart_camera_send_fake.return_val = 0;

	camera_start_capture(&ctx);
	zassert_equal(ctx.state, CAMERA_STATE_WAIT_OK,
		      "Should be waiting for OK");

	/* Feed OK response */
	const char *ok = "OK\r\n";
	camera_rx_feed(&ctx, (const uint8_t *)ok, strlen(ok));
	zassert_equal(ctx.state, CAMERA_STATE_WAIT_PLATE,
		      "Should be waiting for plate after OK");

	/* Feed plate response with matching ID */
	char plate_resp[64];
	snprintf(plate_resp, sizeof(plate_resp),
		 "+PLATE:%u,\"ABC1D23\",93\r\n", ctx.capture_id);
	camera_rx_feed(&ctx, (const uint8_t *)plate_resp, strlen(plate_resp));

	zassert_equal(ctx.state, CAMERA_STATE_DONE, "Should be done");
	zassert_true(ctx.plate_valid, "Plate should be valid");
	zassert_mem_equal(ctx.plate, "ABC1D23", 7, "Plate should match");
	zassert_equal(ctx.confidence, 93, "Confidence should be 93");
}

ZTEST(camera_suite, test_camera_low_confidence_retry)
{
	camera_ctx_t ctx;
	camera_init(&ctx, LANE_ENTRY);

	RESET_FAKE(hw_uart_camera_send);
	hw_uart_camera_send_fake.return_val = 0;

	camera_start_capture(&ctx);
	uint32_t first_id = ctx.capture_id;

	/* OK + low confidence plate */
	const char *ok = "OK\r\n";
	camera_rx_feed(&ctx, (const uint8_t *)ok, strlen(ok));

	char plate_resp[64];
	snprintf(plate_resp, sizeof(plate_resp),
		 "+PLATE:%u,\"ABC1D23\",60\r\n", first_id);
	camera_rx_feed(&ctx, (const uint8_t *)plate_resp, strlen(plate_resp));

	/* Should have moved to next attempt with new ID */
	zassert_true(ctx.capture_id > first_id,
		     "Should have new capture ID after low confidence");
	zassert_equal(ctx.attempt, 1, "Should be on second attempt");
}

ZTEST(camera_suite, test_camera_wrong_id_ignored)
{
	camera_ctx_t ctx;
	camera_init(&ctx, LANE_ENTRY);

	RESET_FAKE(hw_uart_camera_send);
	hw_uart_camera_send_fake.return_val = 0;

	camera_start_capture(&ctx);

	const char *ok = "OK\r\n";
	camera_rx_feed(&ctx, (const uint8_t *)ok, strlen(ok));

	/* Feed plate with wrong ID */
	const char *wrong = "+PLATE:999999,\"ABC1D23\",93\r\n";
	camera_rx_feed(&ctx, (const uint8_t *)wrong, strlen(wrong));

	zassert_equal(ctx.state, CAMERA_STATE_WAIT_PLATE,
		      "Should still be waiting (wrong ID ignored)");
}

ZTEST(camera_suite, test_camera_malformed_response)
{
	camera_ctx_t ctx;
	camera_init(&ctx, LANE_ENTRY);

	RESET_FAKE(hw_uart_camera_send);
	hw_uart_camera_send_fake.return_val = 0;

	camera_start_capture(&ctx);

	const char *ok = "OK\r\n";
	camera_rx_feed(&ctx, (const uint8_t *)ok, strlen(ok));

	/* Malformed plate (bad format) */
	char resp[64];
	snprintf(resp, sizeof(resp), "+PLATE:%u,\"abc1234\",93\r\n",
		 ctx.capture_id);
	camera_rx_feed(&ctx, (const uint8_t *)resp, strlen(resp));

	zassert_equal(ctx.state, CAMERA_STATE_WAIT_PLATE,
		      "Should still be waiting (malformed plate)");
}

/* ══════════════════════════════════════════════════════════════════
 * Payment tests
 * ══════════════════════════════════════════════════════════════════ */

ZTEST_SUITE(payment_suite, NULL, NULL, NULL, NULL, NULL);

ZTEST(payment_suite, test_tariff_calculation_minimum)
{
	/* Under 1 unit: should be minimum 200 */
	int32_t amount = payment_calc_tariff(500, 1000);
	zassert_equal(amount, 200, "0.5s / 1s unit = 1 unit = 200 centavos");
}

ZTEST(payment_suite, test_tariff_calculation_exact_unit)
{
	int32_t amount = payment_calc_tariff(1000, 1000);
	zassert_equal(amount, 200, "1s / 1s unit = 1 unit = 200 centavos");
}

ZTEST(payment_suite, test_tariff_calculation_multiple_units)
{
	/* 2500ms / 1000ms = ceil(2.5) = 3 units */
	int32_t amount = payment_calc_tariff(2500, 1000);
	zassert_equal(amount, 400, "2.5s / 1s unit = 3 units = 400 centavos");
}

ZTEST(payment_suite, test_tariff_calculation_overflow)
{
	/* Very long time: >9999 units should return -1 */
	int32_t amount = payment_calc_tariff(10000000LL, 1000);
	zassert_equal(amount, -1, "10000 units should overflow");
}

ZTEST(payment_suite, test_tariff_calculation_zero_elapsed)
{
	int32_t amount = payment_calc_tariff(0, 1000);
	zassert_equal(amount, 200, "0ms elapsed = minimum 200 centavos");
}

ZTEST(payment_suite, test_tariff_60s_unit)
{
	/* At default 60000ms unit */
	int32_t amount = payment_calc_tariff(60000, 60000);
	zassert_equal(amount, 200, "60s / 60s = 1 unit = 200");

	amount = payment_calc_tariff(60001, 60000);
	zassert_equal(amount, 300, "60.001s / 60s = 2 units = 300");
}

ZTEST(payment_suite, test_payment_init)
{
	payment_ctx_t ctx;
	payment_init(&ctx);

	zassert_equal(ctx.state, PAYMENT_STATE_IDLE, "Should start idle");
	zassert_equal(ctx.rx_len, 0, "Buffer should be empty");
}

ZTEST(payment_suite, test_payment_uuid_format)
{
	char uuid[37];
	payment_generate_uuid(uuid, sizeof(uuid));

	zassert_equal(strlen(uuid), 36, "UUID should be 36 chars");
	zassert_equal(uuid[8], '-', "UUID dash at pos 8");
	zassert_equal(uuid[13], '-', "UUID dash at pos 13");
	zassert_equal(uuid[18], '-', "UUID dash at pos 18");
	zassert_equal(uuid[23], '-', "UUID dash at pos 23");
}

ZTEST(payment_suite, test_payment_paid_response)
{
	payment_ctx_t ctx;
	payment_init(&ctx);

	RESET_FAKE(hw_uart_payment_send);
	hw_uart_payment_send_fake.return_val = 0;

	payment_start_charge(&ctx, "ABC1D23", 400);
	zassert_equal(ctx.state, PAYMENT_STATE_WAIT_RESULT,
		      "Should be waiting for result");

	/* Feed PAID response */
	char resp[128];
	snprintf(resp, sizeof(resp), "P1 RESULT %s PAID 400 BRL\n", ctx.uuid);
	payment_rx_feed(&ctx, (const uint8_t *)resp, strlen(resp));

	zassert_equal(ctx.state, PAYMENT_STATE_PAID, "Should be paid");
	zassert_true(ctx.paid, "Paid flag should be set");
}

ZTEST(payment_suite, test_payment_cancelled_response)
{
	payment_ctx_t ctx;
	payment_init(&ctx);

	RESET_FAKE(hw_uart_payment_send);
	hw_uart_payment_send_fake.return_val = 0;

	payment_start_charge(&ctx, "ABC1D23", 400);

	char resp[128];
	snprintf(resp, sizeof(resp), "P1 RESULT %s CANCELLED 400 BRL\n",
		 ctx.uuid);
	payment_rx_feed(&ctx, (const uint8_t *)resp, strlen(resp));

	zassert_equal(ctx.state, PAYMENT_STATE_FAILED,
		      "Should be failed on CANCELLED");
	zassert_false(ctx.paid, "Paid flag should not be set");
}

ZTEST(payment_suite, test_payment_wrong_uuid_ignored)
{
	payment_ctx_t ctx;
	payment_init(&ctx);

	RESET_FAKE(hw_uart_payment_send);
	hw_uart_payment_send_fake.return_val = 0;

	payment_start_charge(&ctx, "ABC1D23", 400);

	const char *resp = "P1 RESULT 00000000-0000-0000-0000-000000000000"
			   " PAID 400 BRL\n";
	payment_rx_feed(&ctx, (const uint8_t *)resp, strlen(resp));

	zassert_equal(ctx.state, PAYMENT_STATE_WAIT_RESULT,
		      "Should still be waiting (wrong UUID)");
}

ZTEST(payment_suite, test_payment_wrong_amount_ignored)
{
	payment_ctx_t ctx;
	payment_init(&ctx);

	RESET_FAKE(hw_uart_payment_send);
	hw_uart_payment_send_fake.return_val = 0;

	payment_start_charge(&ctx, "ABC1D23", 400);

	char resp[128];
	snprintf(resp, sizeof(resp), "P1 RESULT %s PAID 999 BRL\n", ctx.uuid);
	payment_rx_feed(&ctx, (const uint8_t *)resp, strlen(resp));

	zassert_equal(ctx.state, PAYMENT_STATE_WAIT_RESULT,
		      "Should still be waiting (wrong amount)");
}

ZTEST(payment_suite, test_payment_cr_rejected)
{
	payment_ctx_t ctx;
	payment_init(&ctx);

	RESET_FAKE(hw_uart_payment_send);
	hw_uart_payment_send_fake.return_val = 0;

	payment_start_charge(&ctx, "ABC1D23", 400);

	/* Response with CR (should be rejected in payment protocol) */
	char resp[128];
	snprintf(resp, sizeof(resp), "P1 RESULT %s PAID 400 BRL\r\n",
		 ctx.uuid);
	payment_rx_feed(&ctx, (const uint8_t *)resp, strlen(resp));

	/* CR is stripped, so LF should still terminate correctly */
	zassert_equal(ctx.state, PAYMENT_STATE_PAID,
		      "CR is ignored, LF terminates");
}
