/*
 * camera.c — Camera UART protocol implementation
 *
 * Parses AT-style camera responses. The camera UART uses CRLF termination.
 * Maximum frame: 128 bytes including terminator.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "camera.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <zephyr/kernel.h>

/* Global capture ID counter, shared across both lanes. */
static uint32_t global_capture_id = 1;

void camera_init(camera_ctx_t *ctx, lane_id_t lane)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->lane = lane;
	ctx->state = CAMERA_STATE_IDLE;
}

void camera_reset(camera_ctx_t *ctx)
{
	lane_id_t lane = ctx->lane;
	memset(ctx, 0, sizeof(*ctx));
	ctx->lane = lane;
	ctx->state = CAMERA_STATE_IDLE;
}

bool camera_validate_plate(const char *plate)
{
	if (strlen(plate) != CAMERA_PLATE_LEN) {
		return false;
	}
	for (int i = 0; i < 3; i++) {
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
	for (int i = 5; i < 7; i++) {
		if (plate[i] < '0' || plate[i] > '9') {
			return false;
		}
	}
	return true;
}

static void camera_send_capture_cmd(camera_ctx_t *ctx)
{
	char cmd[32];
	int len = snprintf(cmd, sizeof(cmd), "AT+CAPTURE=%u\r\n",
			   ctx->capture_id);
	hw_uart_camera_send(ctx->lane, (const uint8_t *)cmd, (size_t)len);
}

static void camera_start_next_attempt(camera_ctx_t *ctx);

void camera_start_capture(camera_ctx_t *ctx)
{
	ctx->attempt = 0;
	ctx->plate_valid = false;
	ctx->rx_len = 0;
	ctx->got_ok = false;
	memset(ctx->plate, 0, sizeof(ctx->plate));
	ctx->confidence = 0;

	ctx->capture_id = global_capture_id++;
	ctx->state = CAMERA_STATE_WAIT_OK;
	ctx->deadline_ms = k_uptime_get() + CAMERA_TIMEOUT_MS;

	camera_send_capture_cmd(ctx);
}

static void camera_start_next_attempt(camera_ctx_t *ctx)
{
	ctx->attempt++;
	if (ctx->attempt >= CAMERA_MAX_ATTEMPTS) {
		ctx->state = CAMERA_STATE_FAILED;
		return;
	}

	ctx->rx_len = 0;
	ctx->got_ok = false;
	memset(ctx->plate, 0, sizeof(ctx->plate));
	ctx->confidence = 0;

	ctx->capture_id = global_capture_id++;
	ctx->state = CAMERA_STATE_WAIT_OK;
	ctx->deadline_ms = k_uptime_get() + CAMERA_TIMEOUT_MS;

	camera_send_capture_cmd(ctx);
}

/*
 * Parse a complete line (without CRLF) from the camera.
 * Possible responses:
 *   "OK"
 *   "+PLATE:<id>,\"<plate>\",<confidence>"
 *   "ERROR"
 */
static void camera_process_line(camera_ctx_t *ctx, const char *line, size_t len)
{
	if (ctx->state != CAMERA_STATE_WAIT_OK &&
	    ctx->state != CAMERA_STATE_WAIT_PLATE) {
		return;
	}

	/* "OK" */
	if (len == 2 && line[0] == 'O' && line[1] == 'K') {
		if (ctx->state == CAMERA_STATE_WAIT_OK) {
			ctx->got_ok = true;
			ctx->state = CAMERA_STATE_WAIT_PLATE;
		}
		return;
	}

	/* "ERROR" */
	if (len == 5 && strncmp(line, "ERROR", 5) == 0) {
		return;
	}

	/* "+PLATE:<id>,\"<plate>\",<confidence>" */
	if (len > 7 && strncmp(line, "+PLATE:", 7) == 0) {
		if (ctx->state != CAMERA_STATE_WAIT_PLATE) {
			return;
		}

		const char *p = line + 7;

		/* Parse capture ID */
		const char *comma1 = strchr(p, ',');
		if (comma1 == NULL) {
			return;
		}

		char id_str[16];
		size_t id_len = (size_t)(comma1 - p);
		if (id_len == 0 || id_len >= sizeof(id_str)) {
			return;
		}
		memcpy(id_str, p, id_len);
		id_str[id_len] = '\0';

		if (id_len > 1 && id_str[0] == '0') {
			return;
		}

		char *endptr;
		unsigned long parsed_id = strtoul(id_str, &endptr, 10);
		if (*endptr != '\0' || parsed_id == 0 || parsed_id > 999999999) {
			return;
		}

		if ((uint32_t)parsed_id != ctx->capture_id) {
			return;
		}

		/* Parse plate: ,"<plate>", */
		p = comma1 + 1;
		if (*p != '"') {
			return;
		}
		p++;

		const char *quote_end = strchr(p, '"');
		if (quote_end == NULL) {
			return;
		}

		size_t plate_len = (size_t)(quote_end - p);
		if (plate_len != CAMERA_PLATE_LEN) {
			return;
		}

		char plate_buf[CAMERA_PLATE_LEN + 1];
		memcpy(plate_buf, p, plate_len);
		plate_buf[plate_len] = '\0';

		if (!camera_validate_plate(plate_buf)) {
			return;
		}

		/* Parse confidence */
		p = quote_end + 1;
		if (*p != ',') {
			return;
		}
		p++;

		char *conf_end;
		long conf = strtol(p, &conf_end, 10);
		if (conf_end == p || *conf_end != '\0') {
			return;
		}
		if (conf < 0 || conf > 100) {
			return;
		}

		size_t conf_str_len = (size_t)(conf_end - p);
		if (conf_str_len > 1 && *p == '0') {
			return;
		}

		memcpy(ctx->plate, plate_buf, CAMERA_PLATE_LEN + 1);
		ctx->confidence = (int)conf;

		if (ctx->confidence >= CAMERA_CONFIDENCE_THRESHOLD) {
			ctx->plate_valid = true;
			ctx->state = CAMERA_STATE_DONE;
		} else {
			camera_start_next_attempt(ctx);
		}
	}
}

void camera_rx_feed(camera_ctx_t *ctx, const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uint8_t c = data[i];

		if (ctx->rx_len >= CAMERA_MAX_FRAME) {
			/* Overlong frame: discard through LF */
			if (c == '\n') {
				ctx->rx_len = 0;
			}
			continue;
		}

		ctx->rx_buf[ctx->rx_len++] = c;

		/* Check for CRLF */
		if (c == '\n' && ctx->rx_len >= 2 &&
		    ctx->rx_buf[ctx->rx_len - 2] == '\r') {
			/* Null-terminate, excluding CRLF */
			ctx->rx_buf[ctx->rx_len - 2] = '\0';
			size_t line_len = ctx->rx_len - 2;

			camera_process_line(ctx, (const char *)ctx->rx_buf,
					    line_len);

			ctx->rx_len = 0;
		}
	}
}

bool camera_process(camera_ctx_t *ctx, int64_t now_ms)
{
	if (ctx->state == CAMERA_STATE_DONE ||
	    ctx->state == CAMERA_STATE_FAILED ||
	    ctx->state == CAMERA_STATE_IDLE) {
		return true;
	}

	/* Check timeout */
	if (now_ms >= ctx->deadline_ms) {
		camera_start_next_attempt(ctx);
		if (ctx->state == CAMERA_STATE_FAILED ||
		    ctx->state == CAMERA_STATE_DONE) {
			return true;
		}
	}

	return false;
}
