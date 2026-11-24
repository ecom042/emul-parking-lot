/*
 * display.h — Non-blocking ST7789V SPI controller and payment screen (REQ-12)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PARKING_DISPLAY_H_
#define PARKING_DISPLAY_H_

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_WIDTH  240U
#define DISPLAY_HEIGHT 320U

/* Reference startup delays (virtual milliseconds) */
#define DISPLAY_RESET_ASSERT_MS 10
#define DISPLAY_RESET_WAKE_MS   120
#define DISPLAY_SWRESET_WAIT_MS 150
#define DISPLAY_SLPOUT_WAIT_MS  120

typedef enum {
	DISP_STATE_UNINIT = 0,
	DISP_STATE_RESET_ASSERT,
	DISP_STATE_RESET_WAKE,
	DISP_STATE_SWRESET_WAIT,
	DISP_STATE_SLPOUT_WAIT,
	DISP_STATE_READY,
	DISP_STATE_ERROR
} display_state_t;

typedef struct {
	display_state_t state;
	int64_t step_ms;
} display_ctx_t;

typedef void (*display_yield_fn)(void);

int display_start_init(display_ctx_t *ctx, int64_t now_ms);
void display_service(display_ctx_t *ctx, int64_t now_ms);

/* Called between display rows so ADC and safety stay inside their deadlines. */
void display_set_yield(display_yield_fn fn);
bool display_is_ready(const display_ctx_t *ctx);
bool display_has_error(const display_ctx_t *ctx);

/* Step 1 before tariff freeze: render recognized plate */
int display_render_plate(display_ctx_t *ctx, const char *plate);

/* Step 2 after tariff freeze: render BRL <r>.<cc>, SIMULATED PAYMENT, NOT PIX */
int display_render_charge_details(display_ctx_t *ctx, uint32_t amount_centavos);

#endif /* PARKING_DISPLAY_H_ */
