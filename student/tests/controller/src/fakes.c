/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(int, hw_io_init);
DEFINE_FAKE_VALUE_FUNC(int, hw_motor_drive, const struct lane_cfg *, bool);
DEFINE_FAKE_VALUE_FUNC(int, hw_motor_stop, const struct lane_cfg *);
DEFINE_FAKE_VALUE_FUNC(bool, hw_motor_running, const struct lane_cfg *);
DEFINE_FAKE_VALUE_FUNC(int, hw_lights_set, const struct lane_cfg *, bool, bool);
DEFINE_FAKE_VALUE_FUNC(int, hw_buzzer_pulse, const struct lane_cfg *);
DEFINE_FAKE_VALUE_FUNC(int, hw_beam_read, const struct lane_cfg *, bool *);
DEFINE_FAKE_VALUE_FUNC(uint32_t, hw_beam_take_edges, const struct lane_cfg *);
DEFINE_FAKE_VALUE_FUNC(int, hw_limits_read, const struct lane_cfg *, bool *,
		       bool *);
DEFINE_FAKE_VALUE_FUNC(int, hw_adc_init);
DEFINE_FAKE_VALUE_FUNC(int, hw_adc_poll, int64_t, struct hw_adc_sample *);
DEFINE_FAKE_VALUE_FUNC(int, hw_uart_init);
DEFINE_FAKE_VALUE_FUNC(int, hw_uart_send, enum hw_uart_port, const char *);
DEFINE_FAKE_VALUE_FUNC(bool, hw_uart_poll_plate, enum hw_uart_port,
		       struct at_plate *);
DEFINE_FAKE_VALUE_FUNC(bool, hw_uart_poll_payment, struct p1_result *);
DEFINE_FAKE_VALUE_FUNC(int, st7789_start);
DEFINE_FAKE_VALUE_FUNC(int, st7789_show_plate, const char *);
DEFINE_FAKE_VALUE_FUNC(int, st7789_show_charge, uint32_t);

struct mundo mundo;

void mundo_reset(void)
{
	memset(&mundo, 0, sizeof(mundo));
	for (size_t i = 0; i < LANE_COUNT; i++) {
		mundo.lower[i] = true;
	}
}

void mundo_placa(enum hw_uart_port port, uint32_t id, const char *plate,
		 uint8_t confidence)
{
	struct at_plate *p = &mundo.plates[port][mundo.n_plates[port]++];

	p->id = id;
	strncpy(p->plate, plate, sizeof(p->plate) - 1);
	p->confidence = confidence;
}

void mundo_pagamento(enum p1_status status, const char *uuid, uint32_t cents)
{
	struct p1_result *r = &mundo.payments[mundo.n_payments++];

	r->status = status;
	r->error = P1_ERROR_NONE;
	strncpy(r->uuid, uuid, sizeof(r->uuid) - 1);
	r->cents = cents;
}

static int limits_custom(const struct lane_cfg *cfg, bool *upper, bool *lower)
{
	if (mundo.limits_rc[cfg->id] < 0) {
		return mundo.limits_rc[cfg->id];
	}
	*upper = mundo.upper[cfg->id];
	*lower = mundo.lower[cfg->id];
	return 0;
}

static uint32_t edges_custom(const struct lane_cfg *cfg)
{
	uint32_t edges = mundo.edges[cfg->id];

	mundo.edges[cfg->id] = 0U;
	return edges;
}

static int adc_custom(int64_t now_ms, struct hw_adc_sample *out)
{
	ARG_UNUSED(now_ms);
	if (mundo.adc_rc > 0) {
		*out = mundo.adc;
	}
	return mundo.adc_rc;
}

/* Retira o primeiro quadro da fila (FIFO). */
static bool plate_custom(enum hw_uart_port port, struct at_plate *out)
{
	if (mundo.n_plates[port] == 0U) {
		return false;
	}
	*out = mundo.plates[port][0];
	mundo.n_plates[port]--;
	memmove(&mundo.plates[port][0], &mundo.plates[port][1],
		mundo.n_plates[port] * sizeof(mundo.plates[port][0]));
	return true;
}

static bool payment_custom(struct p1_result *out)
{
	if (mundo.n_payments == 0U) {
		return false;
	}
	*out = mundo.payments[0];
	mundo.n_payments--;
	memmove(&mundo.payments[0], &mundo.payments[1],
		mundo.n_payments * sizeof(mundo.payments[0]));
	return true;
}

static int uart_custom(enum hw_uart_port port, const char *line)
{
	mundo.uart_port = port;
	strncpy(mundo.uart_line, line, sizeof(mundo.uart_line) - 1);
	return 0;
}

void fakes_reset(void)
{
	RESET_FAKE(hw_io_init);
	RESET_FAKE(hw_motor_drive);
	RESET_FAKE(hw_motor_stop);
	RESET_FAKE(hw_motor_running);
	RESET_FAKE(hw_lights_set);
	RESET_FAKE(hw_buzzer_pulse);
	RESET_FAKE(hw_beam_read);
	RESET_FAKE(hw_beam_take_edges);
	RESET_FAKE(hw_limits_read);
	RESET_FAKE(hw_adc_init);
	RESET_FAKE(hw_adc_poll);
	RESET_FAKE(hw_uart_init);
	RESET_FAKE(hw_uart_send);
	RESET_FAKE(hw_uart_poll_plate);
	RESET_FAKE(hw_uart_poll_payment);
	RESET_FAKE(st7789_start);
	RESET_FAKE(st7789_show_plate);
	RESET_FAKE(st7789_show_charge);
	FFF_RESET_HISTORY();

	hw_limits_read_fake.custom_fake = limits_custom;
	hw_beam_take_edges_fake.custom_fake = edges_custom;
	hw_adc_poll_fake.custom_fake = adc_custom;
	hw_uart_poll_plate_fake.custom_fake = plate_custom;
	hw_uart_poll_payment_fake.custom_fake = payment_custom;
	hw_uart_send_fake.custom_fake = uart_custom;
}
