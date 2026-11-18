/*
 * hw.h — Thin hardware abstraction layer over Zephyr RTOS drivers
 *
 * Isolates direct Zephyr GPIO/EXTI, ADC4, UART and SPI calls behind
 * 1-line pass-through functions so all controller, protocol, ADC,
 * display and safety state machines can be unit-tested with FFF fakes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PARKING_HW_H_
#define PARKING_HW_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
	LANE_ENTRY = 0,
	LANE_EXIT = 1,
	LANE_COUNT = 2
} lane_id_t;

typedef enum {
	HW_OUT_ENTRY_MOTOR_EN = 0,
	HW_OUT_ENTRY_MOTOR_DIR,
	HW_OUT_ENTRY_RED,
	HW_OUT_ENTRY_GREEN,
	HW_OUT_ENTRY_BUZZER,
	HW_OUT_EXIT_MOTOR_EN,
	HW_OUT_EXIT_MOTOR_DIR,
	HW_OUT_EXIT_RED,
	HW_OUT_EXIT_GREEN,
	HW_OUT_EXIT_BUZZER,
	HW_OUT_DISPLAY_DC,
	HW_OUT_DISPLAY_RESET,
	HW_OUT_COUNT
} hw_out_pin_t;

typedef enum {
	HW_IN_ENTRY_UPPER = 0,
	HW_IN_ENTRY_LOWER,
	HW_IN_ENTRY_BEAM,
	HW_IN_EXIT_UPPER,
	HW_IN_EXIT_LOWER,
	HW_IN_EXIT_BEAM,
	HW_IN_COUNT
} hw_in_pin_t;

typedef enum {
	HW_UART_ENTRY_CAMERA = 0,
	HW_UART_EXIT_CAMERA,
	HW_UART_PAYMENT,
	HW_UART_COUNT
} hw_uart_id_t;

typedef void (*hw_work_fn_t)(void);

/* GPIO outputs */
bool hw_out_is_ready(hw_out_pin_t pin);
int hw_out_configure_inactive(hw_out_pin_t pin);
int hw_out_set(hw_out_pin_t pin, bool active);

/* GPIO inputs and deferred EXTI work */
bool hw_in_is_ready(hw_in_pin_t pin);
int hw_in_configure(hw_in_pin_t pin);
void hw_exti_set_work_handler(hw_work_fn_t handler);
int hw_in_register_callback(hw_in_pin_t pin);
int hw_in_configure_interrupt(hw_in_pin_t pin);
int hw_in_get(hw_in_pin_t pin);

/* Critical section and virtual-time pacing */
unsigned int hw_irq_lock(void);
void hw_irq_unlock(unsigned int key);
int64_t hw_uptime_ms(void);
void hw_wait_tick(void);

/* UART polling byte transport */
bool hw_uart_is_ready(hw_uart_id_t id);
int hw_uart_poll_in(hw_uart_id_t id, uint8_t *byte);
void hw_uart_poll_out(hw_uart_id_t id, uint8_t byte);

/* Shared asynchronous ADC4 single-conversion transport */
bool hw_adc_is_ready(void);
int hw_adc_setup_channel(lane_id_t lane);
int hw_adc_start_async(lane_id_t lane);
void hw_adc_check_async(unsigned int *signaled, int *result, int16_t *raw_sample);

/* Four-wire SPI1 transport for ST7789V display */
bool hw_spi_is_ready(void);
int hw_spi_write(uint8_t *data, size_t len);

#endif /* PARKING_HW_H_ */
