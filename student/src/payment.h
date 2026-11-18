/*
 * payment.h — Payment terminal UART protocol (P1)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PAYMENT_H
#define PAYMENT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define PAYMENT_MAX_FRAME    128
#define PAYMENT_UUID_LEN     36
#define PAYMENT_PLATE_LEN    7
#define PAYMENT_MAX_SENDS    3
#define PAYMENT_TIMEOUT_MS   1000
#define PAYMENT_MAX_AMOUNT   100000000  /* 1000000 centavos max */

typedef enum {
	PAYMENT_STATE_IDLE,
	PAYMENT_STATE_WAIT_RESULT,
	PAYMENT_STATE_PAID,
	PAYMENT_STATE_FAILED,
} payment_state_t;

typedef struct {
	/* Parser state */
	uint8_t rx_buf[PAYMENT_MAX_FRAME];
	size_t rx_len;

	/* Protocol state */
	payment_state_t state;
	char uuid[PAYMENT_UUID_LEN + 1];
	char plate[PAYMENT_PLATE_LEN + 1];
	uint32_t amount_centavos;
	int send_count;        /* 0-based, up to PAYMENT_MAX_SENDS */
	int64_t deadline_ms;   /* absolute virtual time deadline */

	/* Result */
	bool paid;
} payment_ctx_t;

/* Initialize payment context. */
void payment_init(payment_ctx_t *ctx);

/* Reset payment context. */
void payment_reset(payment_ctx_t *ctx);

/* Generate a new UUID for a charge. */
void payment_generate_uuid(char *uuid_buf, size_t buf_size);

/* Start a payment charge. Sends CHARGE command. */
void payment_start_charge(payment_ctx_t *ctx, const char *plate,
			   uint32_t amount_centavos);

/* Cancel the current active payment. */
void payment_cancel(payment_ctx_t *ctx);

/* Feed received bytes into the parser. */
void payment_rx_feed(payment_ctx_t *ctx, const uint8_t *data, size_t len);

/* Process the payment protocol (call from main loop).
 * Returns true if the payment is settled (check state for PAID or FAILED). */
bool payment_process(payment_ctx_t *ctx, int64_t now_ms);

/* Calculate tariff in centavos based on elapsed time. Returns -1 if overflow. */
int32_t payment_calc_tariff(int64_t elapsed_ms, int32_t unit_ms);

#endif /* PAYMENT_H */
