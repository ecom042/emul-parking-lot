/*
 * payment.c — Payment terminal UART protocol implementation
 *
 * Uses LF termination (no CR). Fields separated by single space.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "payment.h"
#include "hw.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

/* Simple UUID counter for generating unique UUIDs. */
static uint32_t uuid_counter;

void payment_init(payment_ctx_t *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->state = PAYMENT_STATE_IDLE;
}

void payment_reset(payment_ctx_t *ctx)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->state = PAYMENT_STATE_IDLE;
}

void payment_generate_uuid(char *uuid_buf, size_t buf_size)
{
	/* Generate a deterministic but unique UUID based on counter.
	 * Format: 8-4-4-4-12 hex characters. */
	uuid_counter++;
	snprintf(uuid_buf, buf_size,
		 "%08x-%04x-%04x-%04x-%012x",
		 uuid_counter,
		 (uuid_counter >> 4) & 0xFFFF,
		 0x4000 | (uuid_counter & 0x0FFF),
		 0x8000 | ((uuid_counter >> 8) & 0x3FFF),
		 uuid_counter * 0x1234567);
}

static void payment_send_charge(payment_ctx_t *ctx)
{
	char cmd[192];
	int len = snprintf(cmd, sizeof(cmd), "P1 CHARGE %s %s %u BRL\n",
			   ctx->uuid, ctx->plate, ctx->amount_centavos);
	hw_uart_payment_send((const uint8_t *)cmd, (size_t)len);
}

void payment_start_charge(payment_ctx_t *ctx, const char *plate,
			   uint32_t amount_centavos)
{
	ctx->rx_len = 0;
	ctx->paid = false;
	ctx->send_count = 0;

	strncpy(ctx->plate, plate, PAYMENT_PLATE_LEN);
	ctx->plate[PAYMENT_PLATE_LEN] = '\0';
	ctx->amount_centavos = amount_centavos;

	/* Generate a new UUID for this charge */
	payment_generate_uuid(ctx->uuid, sizeof(ctx->uuid));

	ctx->state = PAYMENT_STATE_WAIT_RESULT;
	ctx->send_count = 1;
	ctx->deadline_ms = k_uptime_get() + PAYMENT_TIMEOUT_MS;

	payment_send_charge(ctx);
}

void payment_cancel(payment_ctx_t *ctx)
{
	if (ctx->state != PAYMENT_STATE_WAIT_RESULT) {
		return;
	}

	/* Save UUID for the CANCEL command, then invalidate it
	 * so stale responses are rejected. */
	char uuid_copy[PAYMENT_UUID_LEN + 1];
	memcpy(uuid_copy, ctx->uuid, PAYMENT_UUID_LEN + 1);

	/* Invalidate UUID first (spec: invalidate before sending CANCEL) */
	memset(ctx->uuid, 0, sizeof(ctx->uuid));
	ctx->state = PAYMENT_STATE_FAILED;

	/* Now send CANCEL with the saved UUID */
	char cmd[64];
	int len = snprintf(cmd, sizeof(cmd), "P1 CANCEL %s\n", uuid_copy);
	hw_uart_payment_send((const uint8_t *)cmd, (size_t)len);
}

/*
 * Parse a complete line (without LF) from the payment terminal.
 * Expected: "P1 RESULT <uuid> PAID <amount> BRL"
 *           "P1 RESULT <uuid> CANCELLED <amount> BRL"
 *           "P1 ERROR <code>"
 */
static void payment_process_line(payment_ctx_t *ctx, const char *line,
				 size_t len)
{
	if (ctx->state != PAYMENT_STATE_WAIT_RESULT) {
		return;
	}

	/* Must start with "P1 " */
	if (len < 3 || strncmp(line, "P1 ", 3) != 0) {
		return;
	}

	const char *p = line + 3;

	/* "RESULT <uuid> PAID <amount> BRL" */
	if (strncmp(p, "RESULT ", 7) == 0) {
		p += 7;

		/* Parse UUID */
		if (strlen(p) < PAYMENT_UUID_LEN) {
			return;
		}

		char parsed_uuid[PAYMENT_UUID_LEN + 1];
		memcpy(parsed_uuid, p, PAYMENT_UUID_LEN);
		parsed_uuid[PAYMENT_UUID_LEN] = '\0';

		/* UUID must match our active one */
		if (strncmp(parsed_uuid, ctx->uuid, PAYMENT_UUID_LEN) != 0) {
			return; /* Stale or wrong UUID */
		}

		p += PAYMENT_UUID_LEN;
		if (*p != ' ') {
			return;
		}
		p++;

		/* Parse result status */
		if (strncmp(p, "PAID ", 5) == 0) {
			p += 5;

			/* Parse amount */
			char *end;
			unsigned long amount = strtoul(p, &end, 10);
			if (end == p) {
				return;
			}

			/* Check amount matches */
			if (amount != ctx->amount_centavos) {
				return; /* Amount mismatch */
			}

			/* Must end with " BRL" */
			if (strncmp(end, " BRL", 4) != 0 || *(end + 4) != '\0') {
				return;
			}

			ctx->paid = true;
			ctx->state = PAYMENT_STATE_PAID;
		} else if (strncmp(p, "CANCELLED ", 10) == 0) {
			/* Payment cancelled — do not open */
			ctx->state = PAYMENT_STATE_FAILED;
		}
		/* Anything else (MALFORMED etc.) is ignored / treated as failure */
		return;
	}

	/* "ERROR <code>" */
	if (strncmp(p, "ERROR ", 6) == 0) {
		/* Error responses are diagnostics, not authorization */
		return;
	}
}

void payment_rx_feed(payment_ctx_t *ctx, const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uint8_t c = data[i];

		/* Reject CR in payment protocol */
		if (c == '\r') {
			continue;
		}

		if (ctx->rx_len >= PAYMENT_MAX_FRAME) {
			/* Overlong frame: discard through LF */
			if (c == '\n') {
				ctx->rx_len = 0;
			}
			continue;
		}

		if (c == '\n') {
			/* Complete line */
			ctx->rx_buf[ctx->rx_len] = '\0';
			payment_process_line(ctx, (const char *)ctx->rx_buf,
					     ctx->rx_len);
			ctx->rx_len = 0;
		} else {
			ctx->rx_buf[ctx->rx_len++] = c;
		}
	}
}

bool payment_process(payment_ctx_t *ctx, int64_t now_ms)
{
	if (ctx->state == PAYMENT_STATE_PAID ||
	    ctx->state == PAYMENT_STATE_FAILED ||
	    ctx->state == PAYMENT_STATE_IDLE) {
		return true;
	}

	/* Check timeout: resend with same UUID */
	if (now_ms >= ctx->deadline_ms) {
		if (ctx->send_count < PAYMENT_MAX_SENDS) {
			ctx->send_count++;
			ctx->deadline_ms = k_uptime_get() + PAYMENT_TIMEOUT_MS;
			payment_send_charge(ctx);
		} else {
			/* Exhausted retries */
			ctx->state = PAYMENT_STATE_FAILED;
			return true;
		}
	}

	return false;
}

int32_t payment_calc_tariff(int64_t elapsed_ms, int32_t unit_ms)
{
	if (elapsed_ms < 0) {
		elapsed_ms = 0;
	}
	if (unit_ms <= 0) {
		return -1;
	}

	/* units = max(1, ceil(elapsed_ms / unit_ms)) */
	int64_t units = (elapsed_ms + unit_ms - 1) / unit_ms;
	if (units < 1) {
		units = 1;
	}

	/* Above 9999 units: overflow */
	if (units > 9999) {
		return -1;
	}

	/* amount = 200 + (units - 1) * 100 */
	int32_t amount = 200 + (int32_t)(units - 1) * 100;
	return amount;
}
