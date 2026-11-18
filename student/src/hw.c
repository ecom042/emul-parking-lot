/*
 * hw.c — Thin Zephyr RTOS hardware adapter for parking_lot_u5
 *
 * Every device is referenced exclusively through its devicetree alias.
 * EXTI callbacks submit deferred work to the Zephyr system workqueue.
 * ADC4 buffers, sequence and poll signal live in static storage so they
 * outlive arbitrarily late completions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hw.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>

#define ENTRY_DIST_NODE DT_ALIAS(entry_distance)
#define EXIT_DIST_NODE  DT_ALIAS(exit_distance)
#define DISPLAY_NODE    DT_ALIAS(payment_display)

static const struct gpio_dt_spec out_pins[HW_OUT_COUNT] = {
	[HW_OUT_ENTRY_MOTOR_EN]  = GPIO_DT_SPEC_GET(DT_ALIAS(entry_motor_enable), gpios),
	[HW_OUT_ENTRY_MOTOR_DIR] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_motor_direction), gpios),
	[HW_OUT_ENTRY_RED]       = GPIO_DT_SPEC_GET(DT_ALIAS(entry_light_red), gpios),
	[HW_OUT_ENTRY_GREEN]     = GPIO_DT_SPEC_GET(DT_ALIAS(entry_light_green), gpios),
	[HW_OUT_ENTRY_BUZZER]    = GPIO_DT_SPEC_GET(DT_ALIAS(entry_buzzer), gpios),
	[HW_OUT_EXIT_MOTOR_EN]   = GPIO_DT_SPEC_GET(DT_ALIAS(exit_motor_enable), gpios),
	[HW_OUT_EXIT_MOTOR_DIR]  = GPIO_DT_SPEC_GET(DT_ALIAS(exit_motor_direction), gpios),
	[HW_OUT_EXIT_RED]        = GPIO_DT_SPEC_GET(DT_ALIAS(exit_light_red), gpios),
	[HW_OUT_EXIT_GREEN]      = GPIO_DT_SPEC_GET(DT_ALIAS(exit_light_green), gpios),
	[HW_OUT_EXIT_BUZZER]     = GPIO_DT_SPEC_GET(DT_ALIAS(exit_buzzer), gpios),
	[HW_OUT_DISPLAY_DC]      = GPIO_DT_SPEC_GET(DISPLAY_NODE, dc_gpios),
	[HW_OUT_DISPLAY_RESET]   = GPIO_DT_SPEC_GET(DISPLAY_NODE, reset_gpios),
};

static const struct gpio_dt_spec in_pins[HW_IN_COUNT] = {
	[HW_IN_ENTRY_UPPER] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_limit_upper), gpios),
	[HW_IN_ENTRY_LOWER] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_limit_lower), gpios),
	[HW_IN_ENTRY_BEAM]  = GPIO_DT_SPEC_GET(DT_ALIAS(entry_beam), gpios),
	[HW_IN_EXIT_UPPER]  = GPIO_DT_SPEC_GET(DT_ALIAS(exit_limit_upper), gpios),
	[HW_IN_EXIT_LOWER]  = GPIO_DT_SPEC_GET(DT_ALIAS(exit_limit_lower), gpios),
	[HW_IN_EXIT_BEAM]   = GPIO_DT_SPEC_GET(DT_ALIAS(exit_beam), gpios),
};

static struct gpio_callback in_cbs[HW_IN_COUNT];
static struct k_work exti_work;
static hw_work_fn_t exti_work_cb;
static K_SEM_DEFINE(loop_wake_sem, 0, 1);

static const struct device *const uart_devs[HW_UART_COUNT] = {
	[HW_UART_ENTRY_CAMERA] = DEVICE_DT_GET(DT_ALIAS(entry_camera)),
	[HW_UART_EXIT_CAMERA]  = DEVICE_DT_GET(DT_ALIAS(exit_camera)),
	[HW_UART_PAYMENT]      = DEVICE_DT_GET(DT_ALIAS(payment_terminal)),
};

static const struct device *const adc_dev =
	DEVICE_DT_GET(DT_PARENT(ENTRY_DIST_NODE));

static const struct adc_channel_cfg adc_ch_cfg[LANE_COUNT] = {
	[LANE_ENTRY] = ADC_CHANNEL_CFG_DT(ENTRY_DIST_NODE),
	[LANE_EXIT]  = ADC_CHANNEL_CFG_DT(EXIT_DIST_NODE),
};

static int16_t adc_sample_buf;
static struct k_poll_signal adc_poll_sig =
	K_POLL_SIGNAL_INITIALIZER(adc_poll_sig);
static struct adc_sequence adc_seq = {
	.buffer = &adc_sample_buf,
	.buffer_size = sizeof(adc_sample_buf),
	.resolution = 12,
};

static const struct spi_dt_spec display_spi = SPI_DT_SPEC_GET(
	DISPLAY_NODE,
	SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER,
	0);

static void exti_work_entry(struct k_work *work)
{
	(void)work;
	exti_work_cb();
	k_sem_give(&loop_wake_sem);
}

static void exti_gpio_isr(const struct device *dev, struct gpio_callback *cb,
			  uint32_t pins)
{
	(void)dev;
	(void)cb;
	(void)pins;
	(void)k_work_submit(&exti_work);
}

bool hw_out_is_ready(hw_out_pin_t pin)
{
	return gpio_is_ready_dt(&out_pins[pin]);
}

int hw_out_configure_inactive(hw_out_pin_t pin)
{
	return gpio_pin_configure_dt(&out_pins[pin], GPIO_OUTPUT_INACTIVE);
}

int hw_out_set(hw_out_pin_t pin, bool active)
{
	return gpio_pin_set_dt(&out_pins[pin], (int)active);
}

bool hw_in_is_ready(hw_in_pin_t pin)
{
	return gpio_is_ready_dt(&in_pins[pin]);
}

int hw_in_configure(hw_in_pin_t pin)
{
	return gpio_pin_configure_dt(&in_pins[pin], GPIO_INPUT);
}

void hw_exti_set_work_handler(hw_work_fn_t handler)
{
	exti_work_cb = handler;
	k_work_init(&exti_work, exti_work_entry);
}

int hw_in_register_callback(hw_in_pin_t pin)
{
	gpio_init_callback(&in_cbs[pin], exti_gpio_isr, BIT(in_pins[pin].pin));
	return gpio_add_callback(in_pins[pin].port, &in_cbs[pin]);
}

int hw_in_configure_interrupt(hw_in_pin_t pin)
{
	return gpio_pin_interrupt_configure_dt(&in_pins[pin],
					       GPIO_INT_EDGE_BOTH);
}

int hw_in_get(hw_in_pin_t pin)
{
	return gpio_pin_get_dt(&in_pins[pin]);
}

unsigned int hw_irq_lock(void)
{
	return irq_lock();
}

void hw_irq_unlock(unsigned int key)
{
	irq_unlock(key);
}

int64_t hw_uptime_ms(void)
{
	return k_uptime_get();
}

void hw_wait_tick(void)
{
	(void)k_sem_take(&loop_wake_sem, K_MSEC(1));
}

bool hw_uart_is_ready(hw_uart_id_t id)
{
	return device_is_ready(uart_devs[id]);
}

int hw_uart_poll_in(hw_uart_id_t id, uint8_t *byte)
{
	return uart_poll_in(uart_devs[id], byte);
}

void hw_uart_poll_out(hw_uart_id_t id, uint8_t byte)
{
	uart_poll_out(uart_devs[id], byte);
}

bool hw_adc_is_ready(void)
{
	return device_is_ready(adc_dev);
}

int hw_adc_setup_channel(lane_id_t lane)
{
	return adc_channel_setup(adc_dev, &adc_ch_cfg[lane]);
}

int hw_adc_start_async(lane_id_t lane)
{
	k_poll_signal_reset(&adc_poll_sig);
	adc_seq.channels = BIT(adc_ch_cfg[lane].channel_id);
	return adc_read_async(adc_dev, &adc_seq, &adc_poll_sig);
}

void hw_adc_check_async(unsigned int *signaled, int *result,
			int16_t *raw_sample)
{
	k_poll_signal_check(&adc_poll_sig, signaled, result);
	*raw_sample = adc_sample_buf;
}

bool hw_spi_is_ready(void)
{
	return spi_is_ready_dt(&display_spi);
}

int hw_spi_write(uint8_t *data, size_t len)
{
	struct spi_buf tx_buf = { .buf = data, .len = len };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

	return spi_write_dt(&display_spi, &tx_set);
}
