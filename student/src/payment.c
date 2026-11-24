/*
 * payment.c — Bounded P1 payment UART parser, idempotent retry & tariff
 *
 * Enforces LF-only framing (CR is rejected as malformed through LF),
 * <=128-byte frames, 1000 ms partial-frame assembly deadline, single-space
 * canonical fields, immutable UUID + centavos across up to 3 sends, and
 * UUID invalidation prior to P1 CANCEL transmission.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "payment.h"
#include "hw.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void payment_uart_send(const char *str, size_t len)
{
	for (size_t i = 0U; i < len; i++) {
		hw_uart_poll_out(HW_UART_PAYMENT, (uint8_t)str[i]);
	}
}

static bool parse_canonical_amount(const char *str, size_t len, uint32_t *out)
{
	uint64_t acc = 0ULL;

	if (len == 0U || len > 7U || str[0] == '0') {
		return false;
	}

	for (size_t i = 0U; i < len; i++) {
		if (str[i] < '0' || str[i] > '9') {
			return false;
		}
		acc = (acc * 10ULL) + (uint64_t)(str[i] - '0');
		if (acc > (uint64_t)PAYMENT_MAX_CENTAVOS) {
			return false;
		}
	}

	*out = (uint32_t)acc;
	return true;
}

void payment_init(payment_ctx_t *ctx)
{
	(void)memset(ctx, 0, sizeof(*ctx));
	ctx->state = PAYMENT_STATE_IDLE;
}

void payment_abort_idle(payment_ctx_t *ctx)
{
	(void)memset(ctx->active_uuid, 0, sizeof(ctx->active_uuid));
	(void)memset(ctx->plate, 0, sizeof(ctx->plate));
	ctx->amount_centavos = 0U;
	ctx->send_count = 0U;
	ctx->state = PAYMENT_STATE_IDLE;
}

void payment_generate_uuid(payment_ctx_t *ctx, char *uuid_out)
{
	uint32_t seq;

	ctx->next_seq++;
	seq = ctx->next_seq;

	(void)snprintf(uuid_out, PAYMENT_UUID_LEN + 1U,
		       "%08" PRIx32 "-%04" PRIx32 "-4%03" PRIx32
		       "-8%03" PRIx32 "-%04" PRIx32 "%08" PRIx32,
		       seq,
		       (seq >> 16) & 0xFFFFU,
		       seq & 0x0FFFU,
		       (seq >> 12) & 0x0FFFU,
		       (seq >> 8) & 0xFFFFU,
		       seq * 2654435761U);
}

static void transmit_charge(const payment_ctx_t *ctx)
{
	char cmd[96];

	(void)snprintf(cmd, sizeof(cmd),
		       "P1 CHARGE %s %s %" PRIu32 " BRL\n",
		       ctx->active_uuid, ctx->plate, ctx->amount_centavos);
	payment_uart_send(cmd, strlen(cmd));
}

void payment_start_charge(payment_ctx_t *ctx, const char *plate,
			  uint32_t amount_centavos, int64_t now_ms)
{
	(void)memcpy(ctx->plate, plate, PAYMENT_PLATE_LEN);
	ctx->plate[PAYMENT_PLATE_LEN] = '\0';
	ctx->amount_centavos = amount_centavos;
	payment_generate_uuid(ctx, ctx->active_uuid);

	ctx->send_count = 1U;
	ctx->send_time_ms = now_ms;
	ctx->state = PAYMENT_STATE_WAITING;

	transmit_charge(ctx);
}

void payment_cancel_active(payment_ctx_t *ctx)
{
	char saved_uuid[PAYMENT_UUID_LEN + 1U];
	char cmd[64];

	if (ctx->active_uuid[0] == '\0') {
		ctx->state = PAYMENT_STATE_FAILED;
		return;
	}

	(void)memcpy(saved_uuid, ctx->active_uuid, PAYMENT_UUID_LEN + 1U);

	/* Invalidate active UUID BEFORE transmitting P1 CANCEL */
	(void)memset(ctx->active_uuid, 0, sizeof(ctx->active_uuid));
	ctx->state = PAYMENT_STATE_FAILED;

	(void)snprintf(cmd, sizeof(cmd), "P1 CANCEL %s\n", saved_uuid);
	payment_uart_send(cmd, strlen(cmd));
}

static void handle_frame(payment_ctx_t *ctx, const char *line, size_t len)
{
	const char *p;
	const char *end;
	bool is_paid = false;
	uint32_t parsed_amount = 0U;
	size_t amount_len;

	if (ctx->state != PAYMENT_STATE_WAITING || ctx->active_uuid[0] == '\0') {
		return;
	}

	/* Minimum: "P1 RESULT <36-char-uuid> PAID 1 BRL" = 57 chars */
	if (len < 57U || strncmp(line, "P1 RESULT ", 10U) != 0) {
		return;
	}

	p = line + 10U;
	if (strncmp(p, ctx->active_uuid, PAYMENT_UUID_LEN) != 0) {
		return;
	}
	p += PAYMENT_UUID_LEN;
	if (*p != ' ') {
		return;
	}
	p++;

	if (strncmp(p, "PAID ", 5U) == 0) {
		is_paid = true;
		p += 5U;
	} else if (strncmp(p, "CANCELLED ", 10U) == 0) {
		is_paid = false;
		p += 10U;
	} else {
		return;
	}

	end = line + len;
	if ((size_t)(end - p) < 5U || strncmp(end - 4U, " BRL", 4U) != 0) {
		return;
	}

	amount_len = (size_t)((end - 4U) - p);
	if (!parse_canonical_amount(p, amount_len, &parsed_amount)) {
		return;
	}
	if (parsed_amount != ctx->amount_centavos) {
		return;
	}

	(void)memset(ctx->active_uuid, 0, sizeof(ctx->active_uuid));
	ctx->state = is_paid ? PAYMENT_STATE_PAID : PAYMENT_STATE_FAILED;
}

static void check_partial_deadline(payment_ctx_t *ctx, int64_t now_ms)
{
	if (ctx->in_frame && !ctx->discard &&
	    ((now_ms - ctx->frame_start_ms) >= PAYMENT_FRAME_DEADLINE_MS)) {
		ctx->discard = true;
		ctx->rx_len = 0U;
	}
}

void payment_rx_byte(payment_ctx_t *ctx, uint8_t byte, int64_t now_ms)
{
	check_partial_deadline(ctx, now_ms);

	if (byte == (uint8_t)'\n') {
		if (!ctx->discard && ctx->rx_len > 0U) {
			ctx->rx_buf[ctx->rx_len] = '\0';
			handle_frame(ctx, ctx->rx_buf, ctx->rx_len);
		}
		ctx->rx_len = 0U;
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

	/* Reject CR (0x0D) and any non-printable ASCII or >127 body bytes */
	if (byte < 0x20U || byte > 0x7EU || ctx->rx_len >= PAYMENT_MAX_BODY_BYTES) {
		ctx->discard = true;
		ctx->rx_len = 0U;
		return;
	}

	ctx->rx_buf[ctx->rx_len++] = (char)byte;
}

void payment_service(payment_ctx_t *ctx, int64_t now_ms)
{
	uint8_t byte = 0U;

	check_partial_deadline(ctx, now_ms);

	for (size_t i = 0U; i < PAYMENT_RX_POLL_BUDGET; i++) {
		if (hw_uart_poll_in(HW_UART_PAYMENT, &byte) != 0) {
			break;
		}
		payment_rx_byte(ctx, byte, now_ms);
	}

	if (ctx->state == PAYMENT_STATE_WAITING &&
	    ((now_ms - ctx->send_time_ms) >= PAYMENT_TIMEOUT_MS)) {
		if (ctx->send_count < PAYMENT_MAX_SENDS) {
			ctx->send_count++;
			ctx->send_time_ms = now_ms;
			transmit_charge(ctx);
		} else {
			payment_cancel_active(ctx);
		}
	}
}

int32_t payment_calc_tariff(int64_t elapsed_ms, int32_t unit_ms)
{
	int64_t units;

	if (unit_ms <= 0) {
		return -1;
	}

	if (elapsed_ms <= 0) {
		units = 1;
	} else {
		units = (elapsed_ms + (int64_t)unit_ms - 1) / (int64_t)unit_ms;
	}

	if (units > PAYMENT_MAX_UNITS) {
		return -1;
	}

	return 200 + ((int32_t)(units - 1) * 100);
}
