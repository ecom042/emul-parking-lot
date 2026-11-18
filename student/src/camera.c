/*
 * camera.c — Bounded AT camera UART parser and 3-attempt recognition
 *
 * Enforces CRLF framing, <=128-byte frames, printable ASCII bodies,
 * 1000 ms partial-frame assembly deadline, canonical decimal fields,
 * capture-ID correlation, and up to 3 attempts with confidence >= 85.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "camera.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void uart_send_str(hw_uart_id_t id, const char *str, size_t len)
{
	for (size_t i = 0U; i < len; i++) {
		hw_uart_poll_out(id, (uint8_t)str[i]);
	}
}

static bool parse_canonical_u32(const char *str, size_t len, uint32_t min_val,
				uint32_t max_val, uint32_t *out)
{
	uint64_t acc = 0ULL;

	if (len == 0U || len > 10U) {
		return false;
	}
	if (len > 1U && str[0] == '0') {
		return false;
	}

	for (size_t i = 0U; i < len; i++) {
		if (str[i] < '0' || str[i] > '9') {
			return false;
		}
		acc = (acc * 10ULL) + (uint64_t)(str[i] - '0');
		if (acc > (uint64_t)max_val) {
			return false;
		}
	}

	if (acc < (uint64_t)min_val) {
		return false;
	}

	*out = (uint32_t)acc;
	return true;
}

bool camera_validate_plate(const char *plate)
{
	if (strlen(plate) != CAMERA_PLATE_LEN) {
		return false;
	}
	for (size_t i = 0U; i < 3U; i++) {
		if (plate[i] < 'A' || plate[i] > 'Z') {
			return false;
		}
	}
	if (plate[3] < '0' || plate[3] > '9') {
		return false;
	}
	if (!((plate[4] >= 'A' && plate[4] <= 'Z') ||
	      (plate[4] >= '0' && plate[4] <= '9'))) {
		return false;
	}
	if (plate[5] < '0' || plate[5] > '9' ||
	    plate[6] < '0' || plate[6] > '9') {
		return false;
	}
	return true;
}

static uint32_t alloc_capture_id(camera_ctx_t *ctx)
{
	uint32_t id = ctx->next_id;

	if (ctx->next_id >= CAMERA_MAX_CAPTURE_ID) {
		ctx->next_id = 1U;
	} else {
		ctx->next_id++;
	}
	return id;
}

static void issue_attempt(camera_ctx_t *ctx, int64_t now_ms)
{
	char cmd[32];

	ctx->active_id = alloc_capture_id(ctx);
	ctx->attempt_start_ms = now_ms;
	ctx->state = CAMERA_STATE_WAITING;

	(void)snprintf(cmd, sizeof(cmd), "AT+CAPTURE=%" PRIu32 "\r\n",
		       ctx->active_id);
	uart_send_str(ctx->uart_id, cmd, strlen(cmd));
}

static void advance_or_fail(camera_ctx_t *ctx, int64_t now_ms)
{
	if (ctx->attempt < CAMERA_MAX_ATTEMPTS) {
		ctx->attempt++;
		issue_attempt(ctx, now_ms);
	} else {
		ctx->active_id = 0U;
		ctx->state = CAMERA_STATE_FAILED;
	}
}

void camera_init(camera_ctx_t *ctx, lane_id_t lane)
{
	(void)memset(ctx, 0, sizeof(*ctx));
	ctx->uart_id = (lane == LANE_ENTRY) ?
		       HW_UART_ENTRY_CAMERA : HW_UART_EXIT_CAMERA;
	ctx->state = CAMERA_STATE_IDLE;
	ctx->next_id = 1U;
}

void camera_abort(camera_ctx_t *ctx)
{
	ctx->state = CAMERA_STATE_IDLE;
	ctx->active_id = 0U;
	ctx->attempt = 0U;
	(void)memset(ctx->plate, 0, sizeof(ctx->plate));
	ctx->confidence = 0U;
}

void camera_start(camera_ctx_t *ctx, int64_t now_ms)
{
	(void)memset(ctx->plate, 0, sizeof(ctx->plate));
	ctx->confidence = 0U;
	ctx->attempt = 1U;
	issue_attempt(ctx, now_ms);
}

static void handle_frame(camera_ctx_t *ctx, const char *line, size_t len,
			 int64_t now_ms)
{
	const char *p;
	const char *comma1;
	const char *quote2;
	uint32_t parsed_id = 0U;
	uint32_t parsed_conf = 0U;
	char plate_buf[CAMERA_PLATE_LEN + 1U];
	size_t conf_len;

	if (ctx->state != CAMERA_STATE_WAITING) {
		return;
	}

	/* OK and ERROR do not authorize or restart deadlines */
	if (len <= 7U || strncmp(line, "+PLATE:", 7U) != 0) {
		return;
	}

	p = line + 7U;
	comma1 = strchr(p, ',');
	if (comma1 == NULL) {
		return;
	}

	if (!parse_canonical_u32(p, (size_t)(comma1 - p), 1U,
				 CAMERA_MAX_CAPTURE_ID, &parsed_id)) {
		return;
	}
	if (parsed_id != ctx->active_id) {
		return;
	}

	p = comma1 + 1U;
	if (*p != '"') {
		return;
	}
	p++;

	quote2 = strchr(p, '"');
	if (quote2 == NULL || (size_t)(quote2 - p) != CAMERA_PLATE_LEN) {
		return;
	}

	(void)memcpy(plate_buf, p, CAMERA_PLATE_LEN);
	plate_buf[CAMERA_PLATE_LEN] = '\0';
	if (!camera_validate_plate(plate_buf)) {
		return;
	}

	p = quote2 + 1U;
	if (*p != ',') {
		return;
	}
	p++;

	conf_len = (size_t)((line + len) - p);
	if (!parse_canonical_u32(p, conf_len, 0U, 100U, &parsed_conf)) {
		return;
	}

	(void)memcpy(ctx->plate, plate_buf, CAMERA_PLATE_LEN + 1U);
	ctx->confidence = (uint8_t)parsed_conf;

	if (parsed_conf >= CAMERA_CONFIDENCE_MIN) {
		ctx->active_id = 0U;
		ctx->state = CAMERA_STATE_ACCEPTED;
	} else {
		advance_or_fail(ctx, now_ms);
	}
}

static void check_partial_deadline(camera_ctx_t *ctx, int64_t now_ms)
{
	if (ctx->in_frame && !ctx->discard &&
	    ((now_ms - ctx->frame_start_ms) >= CAMERA_FRAME_DEADLINE_MS)) {
		ctx->discard = true;
		ctx->rx_len = 0U;
		ctx->saw_cr = false;
	}
}

void camera_rx_byte(camera_ctx_t *ctx, uint8_t byte, int64_t now_ms)
{
	check_partial_deadline(ctx, now_ms);

	if (byte == (uint8_t)'\n') {
		if (!ctx->discard && ctx->saw_cr) {
			ctx->rx_buf[ctx->rx_len] = '\0';
			handle_frame(ctx, ctx->rx_buf, ctx->rx_len, now_ms);
		}
		ctx->rx_len = 0U;
		ctx->saw_cr = false;
		ctx->discard = false;
		ctx->in_frame = false;
		return;
	}

	if (ctx->discard) {
		return;
	}

	if (!ctx->in_frame) {
		ctx->in_frame = true;
		ctx->frame_start_ms = now_ms;
	}

	if (ctx->saw_cr) {
		/* Bare CR not immediately followed by LF */
		ctx->discard = true;
		ctx->rx_len = 0U;
		ctx->saw_cr = false;
		return;
	}

	if (byte == (uint8_t)'\r') {
		ctx->saw_cr = true;
		return;
	}

	if (byte < 0x20U || byte > 0x7EU || ctx->rx_len >= CAMERA_MAX_BODY_BYTES) {
		ctx->discard = true;
		ctx->rx_len = 0U;
		return;
	}

	ctx->rx_buf[ctx->rx_len++] = (char)byte;
}

void camera_service(camera_ctx_t *ctx, int64_t now_ms)
{
	uint8_t byte = 0U;

	check_partial_deadline(ctx, now_ms);

	for (size_t i = 0U; i < CAMERA_RX_POLL_BUDGET; i++) {
		if (hw_uart_poll_in(ctx->uart_id, &byte) != 0) {
			break;
		}
		camera_rx_byte(ctx, byte, now_ms);
	}

	if (ctx->state == CAMERA_STATE_WAITING &&
	    ((now_ms - ctx->attempt_start_ms) >= CAMERA_TIMEOUT_MS)) {
		advance_or_fail(ctx, now_ms);
	}
}
