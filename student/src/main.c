/*
 * main.c — Parking lot controller entry point
 *
 * Initializes hardware, registers callbacks, and runs the main
 * control loop for both entry and exit lanes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include "hw.h"
#include "lane.h"
#include "session.h"
#include "display.h"

/* ── Lane contexts ───────────────────────────────────────────────── */

static lane_ctx_t entry_lane;
static lane_ctx_t exit_lane;

/* ── EXTI callback trampolines ───────────────────────────────────── */

static void on_limit_upper(lane_id_t lane, void *data)
{
	lane_ctx_t *ctx = (lane_ctx_t *)data;
	lane_on_limit_upper(ctx);
}

static void on_limit_lower(lane_id_t lane, void *data)
{
	lane_ctx_t *ctx = (lane_ctx_t *)data;
	lane_on_limit_lower(ctx);
}

static void on_beam(lane_id_t lane, void *data)
{
	lane_ctx_t *ctx = (lane_ctx_t *)data;
	lane_on_beam(ctx);
}

/* ── UART receive trampolines ────────────────────────────────────── */

static void on_entry_camera_rx(const uint8_t *data, size_t len, void *user_data)
{
	lane_on_camera_rx(&entry_lane, data, len);
}

static void on_exit_camera_rx(const uint8_t *data, size_t len, void *user_data)
{
	lane_on_camera_rx(&exit_lane, data, len);
}

static void on_payment_rx(const uint8_t *data, size_t len, void *user_data)
{
	lane_on_payment_rx(&exit_lane, data, len);
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(void)
{
	int rc;

	/* Initialize hardware */
	rc = hw_init();
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: hw init always succeeds */
	}

	/* Initialize session table */
	session_init();

	/* Initialize display */
	rc = display_init();
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: display init always succeeds */
	}

	/* Initialize lanes */
	lane_init(&entry_lane, LANE_ENTRY);
	lane_init(&exit_lane, LANE_EXIT);

	/* Register EXTI callbacks */
	rc = hw_register_limit_upper_cb(LANE_ENTRY, on_limit_upper, &entry_lane);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: callback registration always succeeds */
	}
	rc = hw_register_limit_lower_cb(LANE_ENTRY, on_limit_lower, &entry_lane);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: callback registration always succeeds */
	}
	rc = hw_register_beam_cb(LANE_ENTRY, on_beam, &entry_lane);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: callback registration always succeeds */
	}

	rc = hw_register_limit_upper_cb(LANE_EXIT, on_limit_upper, &exit_lane);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: callback registration always succeeds */
	}
	rc = hw_register_limit_lower_cb(LANE_EXIT, on_limit_lower, &exit_lane);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: callback registration always succeeds */
	}
	rc = hw_register_beam_cb(LANE_EXIT, on_beam, &exit_lane);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: callback registration always succeeds */
	}

	/* Initialize UARTs */
	rc = hw_uart_camera_init(LANE_ENTRY, on_entry_camera_rx, NULL);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: UART init always succeeds */
	}
	rc = hw_uart_camera_init(LANE_EXIT, on_exit_camera_rx, NULL);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: UART init always succeeds */
	}
	rc = hw_uart_payment_init(on_payment_rx, NULL);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: UART init always succeeds */
	}

	/* Main control loop */
	while (1) {
		int64_t now = k_uptime_get();

		lane_process(&entry_lane, now);
		lane_process(&exit_lane, now);

		/* Yield to allow other threads to run. Use a small sleep
		 * to avoid busy-waiting while still being responsive. */
		k_msleep(1);
	}

	return 0; /* LCOV_EXCL_LINE: unreachable */
}
