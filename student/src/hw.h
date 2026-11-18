/*
 * hw.h — Hardware abstraction layer
 *
 * Thin wrappers around Zephyr GPIO/EXTI/UART/SPI/ADC drivers.
 * The lane logic calls these instead of Zephyr APIs directly,
 * making the logic testable with FFF fakes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HW_H
#define HW_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ── Lane identifier ─────────────────────────────────────────────── */

typedef enum {
	LANE_ENTRY = 0,
	LANE_EXIT  = 1,
	LANE_COUNT = 2
} lane_id_t;

/* ── GPIO outputs ────────────────────────────────────────────────── */

int hw_motor_enable(lane_id_t lane, bool on);
int hw_motor_direction(lane_id_t lane, bool open);
int hw_light_red(lane_id_t lane, bool on);
int hw_light_green(lane_id_t lane, bool on);
int hw_buzzer(lane_id_t lane, bool on);

/* ── GPIO inputs ─────────────────────────────────────────────────── */

int hw_read_limit_upper(lane_id_t lane);
int hw_read_limit_lower(lane_id_t lane);
int hw_read_beam(lane_id_t lane);

/* ── EXTI callbacks ──────────────────────────────────────────────── */

typedef void (*hw_exti_cb_t)(lane_id_t lane, void *user_data);

int hw_register_limit_upper_cb(lane_id_t lane, hw_exti_cb_t cb, void *data);
int hw_register_limit_lower_cb(lane_id_t lane, hw_exti_cb_t cb, void *data);
int hw_register_beam_cb(lane_id_t lane, hw_exti_cb_t cb, void *data);

/* ── UART ────────────────────────────────────────────────────────── */

typedef void (*hw_uart_rx_cb_t)(const uint8_t *data, size_t len, void *user_data);

int hw_uart_camera_init(lane_id_t lane, hw_uart_rx_cb_t cb, void *data);
int hw_uart_camera_send(lane_id_t lane, const uint8_t *data, size_t len);

int hw_uart_payment_init(hw_uart_rx_cb_t cb, void *data);
int hw_uart_payment_send(const uint8_t *data, size_t len);

/* ── ADC ─────────────────────────────────────────────────────────── */

int hw_adc_init(void);
int hw_adc_read_async(lane_id_t lane);
int hw_adc_check_completion(lane_id_t lane, uint16_t *raw_code);

/* ── SPI display ─────────────────────────────────────────────────── */

int hw_display_init(void);
int hw_display_reset(void);
int hw_display_write_cmd(uint8_t cmd);
int hw_display_write_data(const uint8_t *data, size_t len);
int hw_display_write_pixels(const uint8_t *data, size_t len);

/* ── Hardware global init ────────────────────────────────────────── */

int hw_init(void);

#endif /* HW_H */
