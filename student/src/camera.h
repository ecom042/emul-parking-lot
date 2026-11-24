/*
 * camera.h — Bounded AT camera UART parser and 3-attempt recognition (REQ-02, REQ-11)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PARKING_CAMERA_H_
#define PARKING_CAMERA_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hw.h"

#define CAMERA_MAX_FRAME_BYTES      128U
#define CAMERA_MAX_BODY_BYTES       126U /* 128 including CRLF */
#define CAMERA_PLATE_LEN            7U
#define CAMERA_MAX_ATTEMPTS         3U
#define CAMERA_TIMEOUT_MS           1000
#define CAMERA_FRAME_DEADLINE_MS    1000
#define CAMERA_CONFIDENCE_MIN       85U
#define CAMERA_MAX_CAPTURE_ID       999999999U
#define CAMERA_RX_POLL_BUDGET       64U

typedef enum {
	CAMERA_STATE_IDLE = 0,
	CAMERA_STATE_WAITING,
	CAMERA_STATE_ACCEPTED,
	CAMERA_STATE_FAILED
} camera_state_t;

typedef struct {
	char rx_buf[CAMERA_MAX_BODY_BYTES + 1U];
	size_t rx_len;
	int64_t frame_start_ms;
	bool in_frame;
	bool saw_cr;
	bool discard;

	hw_uart_id_t uart_id;
	camera_state_t state;
	uint32_t next_id;
	uint32_t active_id;
	uint8_t attempt;
	int64_t attempt_start_ms;

	char plate[CAMERA_PLATE_LEN + 1U];
	uint8_t confidence;
} camera_ctx_t;

void camera_init(camera_ctx_t *ctx, lane_id_t lane);
void camera_abort(camera_ctx_t *ctx);
void camera_start(camera_ctx_t *ctx, int64_t now_ms);
void camera_rx_byte(camera_ctx_t *ctx, uint8_t byte, int64_t now_ms);
void camera_service(camera_ctx_t *ctx, int64_t now_ms);
bool camera_validate_plate(const char *plate);

#endif /* PARKING_CAMERA_H_ */
