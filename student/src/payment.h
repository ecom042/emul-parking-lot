/*
 * payment.h — Bounded P1 payment UART parser, retry & tariff (REQ-05, REQ-11, REQ-12)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PARKING_PAYMENT_H_
#define PARKING_PAYMENT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAYMENT_MAX_FRAME_BYTES   128U
#define PAYMENT_MAX_BODY_BYTES    127U /* 128 including LF */
#define PAYMENT_UUID_LEN          36U
#define PAYMENT_PLATE_LEN         7U
#define PAYMENT_MAX_SENDS         3U
#define PAYMENT_TIMEOUT_MS        1000
#define PAYMENT_FRAME_DEADLINE_MS 1000
#define PAYMENT_MAX_UNITS         9999
#define PAYMENT_MAX_CENTAVOS      1000000U
#define PAYMENT_RX_POLL_BUDGET    64U

typedef enum {
	PAYMENT_STATE_IDLE = 0,
	PAYMENT_STATE_WAITING,
	PAYMENT_STATE_PAID,
	PAYMENT_STATE_FAILED
} payment_state_t;

typedef struct {
	char rx_buf[PAYMENT_MAX_BODY_BYTES + 1U];
	size_t rx_len;
	int64_t frame_start_ms;
	bool in_frame;
	bool discard;

	payment_state_t state;
	uint32_t next_seq;
	char active_uuid[PAYMENT_UUID_LEN + 1U];
	char plate[PAYMENT_PLATE_LEN + 1U];
	uint32_t amount_centavos;
	uint8_t send_count;
	int64_t send_time_ms;
} payment_ctx_t;

void payment_init(payment_ctx_t *ctx);
void payment_abort_idle(payment_ctx_t *ctx);
void payment_generate_uuid(payment_ctx_t *ctx, char *uuid_out);
void payment_start_charge(payment_ctx_t *ctx, const char *plate,
			  uint32_t amount_centavos, int64_t now_ms);
void payment_cancel_active(payment_ctx_t *ctx);
void payment_rx_byte(payment_ctx_t *ctx, uint8_t byte, int64_t now_ms);
void payment_service(payment_ctx_t *ctx, int64_t now_ms);
int32_t payment_calc_tariff(int64_t elapsed_ms, int32_t unit_ms);

#endif /* PARKING_PAYMENT_H_ */
