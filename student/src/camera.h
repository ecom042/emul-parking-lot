/*
 * camera.h — Camera UART protocol (AT commands)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CAMERA_H
#define CAMERA_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "hw.h"

#define CAMERA_MAX_FRAME   128
#define CAMERA_PLATE_LEN   7
#define CAMERA_MAX_ATTEMPTS 3
#define CAMERA_TIMEOUT_MS  1000
#define CAMERA_CONFIDENCE_THRESHOLD 85

typedef enum {
	CAMERA_STATE_IDLE,
	CAMERA_STATE_WAIT_OK,
	CAMERA_STATE_WAIT_PLATE,
	CAMERA_STATE_DONE,
	CAMERA_STATE_FAILED,
} camera_state_t;

typedef struct {
	/* Parser state */
	uint8_t rx_buf[CAMERA_MAX_FRAME];
	size_t rx_len;

	/* Protocol state */
	camera_state_t state;
	uint32_t capture_id;
	int attempt;         /* 0-based, up to CAMERA_MAX_ATTEMPTS-1 */
	int64_t deadline_ms; /* absolute virtual time deadline */

	/* Result */
	char plate[CAMERA_PLATE_LEN + 1];
	int confidence;
	bool got_ok;
	bool plate_valid;

	/* Lane */
	lane_id_t lane;
} camera_ctx_t;

/* Initialize camera context for a lane. */
void camera_init(camera_ctx_t *ctx, lane_id_t lane);

/* Reset camera context (clear parser buffer and state). */
void camera_reset(camera_ctx_t *ctx);

/* Start a capture sequence (up to 3 attempts). */
void camera_start_capture(camera_ctx_t *ctx);

/* Feed received bytes into the parser (called from UART ISR context). */
void camera_rx_feed(camera_ctx_t *ctx, const uint8_t *data, size_t len);

/* Process the camera protocol (call from main loop). Returns true if capture
 * sequence is finished (check state for DONE or FAILED). */
bool camera_process(camera_ctx_t *ctx, int64_t now_ms);

/* Check if plate format is valid: [A-Z]{3}[0-9][A-Z0-9][0-9]{2} */
bool camera_validate_plate(const char *plate);

#endif /* CAMERA_H */
