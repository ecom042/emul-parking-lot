/*
 * hw.c — Hardware abstraction layer implementation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hw.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

/* ── GPIO specs from devicetree aliases ──────────────────────────── */

/* Motor outputs */
static const struct gpio_dt_spec motor_enable[] = {
	[LANE_ENTRY] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_motor_enable), gpios),
	[LANE_EXIT]  = GPIO_DT_SPEC_GET(DT_ALIAS(exit_motor_enable), gpios),
};

static const struct gpio_dt_spec motor_direction[] = {
	[LANE_ENTRY] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_motor_direction), gpios),
	[LANE_EXIT]  = GPIO_DT_SPEC_GET(DT_ALIAS(exit_motor_direction), gpios),
};

/* Signal outputs */
static const struct gpio_dt_spec light_red[] = {
	[LANE_ENTRY] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_light_red), gpios),
	[LANE_EXIT]  = GPIO_DT_SPEC_GET(DT_ALIAS(exit_light_red), gpios),
};

static const struct gpio_dt_spec light_green[] = {
	[LANE_ENTRY] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_light_green), gpios),
	[LANE_EXIT]  = GPIO_DT_SPEC_GET(DT_ALIAS(exit_light_green), gpios),
};

static const struct gpio_dt_spec buzzer_pin[] = {
	[LANE_ENTRY] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_buzzer), gpios),
	[LANE_EXIT]  = GPIO_DT_SPEC_GET(DT_ALIAS(exit_buzzer), gpios),
};

/* Sensor inputs (limits and beam) */
static const struct gpio_dt_spec limit_upper[] = {
	[LANE_ENTRY] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_limit_upper), gpios),
	[LANE_EXIT]  = GPIO_DT_SPEC_GET(DT_ALIAS(exit_limit_upper), gpios),
};

static const struct gpio_dt_spec limit_lower[] = {
	[LANE_ENTRY] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_limit_lower), gpios),
	[LANE_EXIT]  = GPIO_DT_SPEC_GET(DT_ALIAS(exit_limit_lower), gpios),
};

static const struct gpio_dt_spec beam_pin[] = {
	[LANE_ENTRY] = GPIO_DT_SPEC_GET(DT_ALIAS(entry_beam), gpios),
	[LANE_EXIT]  = GPIO_DT_SPEC_GET(DT_ALIAS(exit_beam), gpios),
};

/* ── EXTI callbacks ──────────────────────────────────────────────── */

static struct gpio_callback limit_upper_cb_data[LANE_COUNT];
static struct gpio_callback limit_lower_cb_data[LANE_COUNT];
static struct gpio_callback beam_cb_data[LANE_COUNT];

static hw_exti_cb_t limit_upper_user_cb[LANE_COUNT];
static hw_exti_cb_t limit_lower_user_cb[LANE_COUNT];
static hw_exti_cb_t beam_user_cb[LANE_COUNT];

static void *limit_upper_user_data[LANE_COUNT];
static void *limit_lower_user_data[LANE_COUNT];
static void *beam_user_data[LANE_COUNT];

static void limit_upper_isr(const struct device *dev, struct gpio_callback *cb,
			     uint32_t pins)
{
	for (int i = 0; i < LANE_COUNT; i++) {
		if (cb == &limit_upper_cb_data[i] && limit_upper_user_cb[i]) {
			limit_upper_user_cb[i]((lane_id_t)i, limit_upper_user_data[i]);
		}
	}
}

static void limit_lower_isr(const struct device *dev, struct gpio_callback *cb,
			     uint32_t pins)
{
	for (int i = 0; i < LANE_COUNT; i++) {
		if (cb == &limit_lower_cb_data[i] && limit_lower_user_cb[i]) {
			limit_lower_user_cb[i]((lane_id_t)i, limit_lower_user_data[i]);
		}
	}
}

static void beam_isr(const struct device *dev, struct gpio_callback *cb,
		     uint32_t pins)
{
	for (int i = 0; i < LANE_COUNT; i++) {
		if (cb == &beam_cb_data[i] && beam_user_cb[i]) {
			beam_user_cb[i]((lane_id_t)i, beam_user_data[i]);
		}
	}
}

/* ── UART devices ────────────────────────────────────────────────── */

static const struct device *const camera_dev[] = {
	[LANE_ENTRY] = DEVICE_DT_GET(DT_ALIAS(entry_camera)),
	[LANE_EXIT]  = DEVICE_DT_GET(DT_ALIAS(exit_camera)),
};

static const struct device *const payment_dev = DEVICE_DT_GET(DT_ALIAS(payment_terminal));

static hw_uart_rx_cb_t camera_rx_user_cb[LANE_COUNT];
static void *camera_rx_user_data[LANE_COUNT];
static hw_uart_rx_cb_t payment_rx_user_cb;
static void *payment_rx_user_data;

static void camera_uart_isr(const struct device *dev, void *user_data)
{
	lane_id_t lane = (lane_id_t)(uintptr_t)user_data;
	uint8_t buf[8];

	if (!uart_irq_update(dev)) {
		return;
	}
	while (uart_irq_rx_ready(dev)) {
		int len = uart_fifo_read(dev, buf, sizeof(buf));
		if (len > 0 && camera_rx_user_cb[lane]) {
			camera_rx_user_cb[lane](buf, (size_t)len,
						camera_rx_user_data[lane]);
		}
	}
}

static void payment_uart_isr(const struct device *dev, void *user_data)
{
	uint8_t buf[8];

	if (!uart_irq_update(dev)) {
		return;
	}
	while (uart_irq_rx_ready(dev)) {
		int len = uart_fifo_read(dev, buf, sizeof(buf));
		if (len > 0 && payment_rx_user_cb) {
			payment_rx_user_cb(buf, (size_t)len,
					   payment_rx_user_data);
		}
	}
}

/* ── ADC4 ────────────────────────────────────────────────────────── */

static const struct device *const adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc4));

static struct adc_channel_cfg adc_ch_cfg[] = {
	[LANE_ENTRY] = ADC_CHANNEL_CFG_DT(DT_ALIAS(entry_distance)),
	[LANE_EXIT]  = ADC_CHANNEL_CFG_DT(DT_ALIAS(exit_distance)),
};

static int16_t adc_sample_buf[LANE_COUNT];

static struct adc_sequence adc_seq[LANE_COUNT];
static struct adc_sequence_options adc_opts[LANE_COUNT];

static volatile bool adc_busy[LANE_COUNT];
static volatile bool adc_ready[LANE_COUNT];

static struct k_poll_signal adc_signal[LANE_COUNT];

/* ── SPI display ─────────────────────────────────────────────────── */

static const struct device *const spi_dev = DEVICE_DT_GET(DT_BUS(DT_ALIAS(payment_display)));

static const struct spi_cs_control spi_cs = SPI_CS_CONTROL_INIT(
	DT_ALIAS(payment_display), 0);

static struct spi_config spi_cfg = {
	.frequency = DT_PROP(DT_ALIAS(payment_display), spi_max_frequency),
	.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER,
};

static const struct gpio_dt_spec display_dc =
	GPIO_DT_SPEC_GET(DT_ALIAS(payment_display), dc_gpios);
static const struct gpio_dt_spec display_reset_pin =
	GPIO_DT_SPEC_GET(DT_ALIAS(payment_display), reset_gpios);

/* ── GPIO implementation ─────────────────────────────────────────── */

int hw_motor_enable(lane_id_t lane, bool on)
{
	return gpio_pin_set_dt(&motor_enable[lane], on ? 1 : 0);
}

int hw_motor_direction(lane_id_t lane, bool open)
{
	return gpio_pin_set_dt(&motor_direction[lane], open ? 1 : 0);
}

int hw_light_red(lane_id_t lane, bool on)
{
	return gpio_pin_set_dt(&light_red[lane], on ? 1 : 0);
}

int hw_light_green(lane_id_t lane, bool on)
{
	return gpio_pin_set_dt(&light_green[lane], on ? 1 : 0);
}

int hw_buzzer(lane_id_t lane, bool on)
{
	return gpio_pin_set_dt(&buzzer_pin[lane], on ? 1 : 0);
}

int hw_read_limit_upper(lane_id_t lane)
{
	return gpio_pin_get_dt(&limit_upper[lane]);
}

int hw_read_limit_lower(lane_id_t lane)
{
	return gpio_pin_get_dt(&limit_lower[lane]);
}

int hw_read_beam(lane_id_t lane)
{
	return gpio_pin_get_dt(&beam_pin[lane]);
}

/* ── EXTI registration ───────────────────────────────────────────── */

int hw_register_limit_upper_cb(lane_id_t lane, hw_exti_cb_t cb, void *data)
{
	limit_upper_user_cb[lane] = cb;
	limit_upper_user_data[lane] = data;

	int rc = gpio_pin_configure_dt(&limit_upper[lane], GPIO_INPUT);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: driver error unreachable in simulation */
	}
	rc = gpio_pin_interrupt_configure_dt(&limit_upper[lane],
					     GPIO_INT_EDGE_BOTH);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: driver error unreachable in simulation */
	}

	gpio_init_callback(&limit_upper_cb_data[lane], limit_upper_isr,
			   BIT(limit_upper[lane].pin));
	return gpio_add_callback(limit_upper[lane].port,
				 &limit_upper_cb_data[lane]);
}

int hw_register_limit_lower_cb(lane_id_t lane, hw_exti_cb_t cb, void *data)
{
	limit_lower_user_cb[lane] = cb;
	limit_lower_user_data[lane] = data;

	int rc = gpio_pin_configure_dt(&limit_lower[lane], GPIO_INPUT);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: driver error unreachable in simulation */
	}
	rc = gpio_pin_interrupt_configure_dt(&limit_lower[lane],
					     GPIO_INT_EDGE_BOTH);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: driver error unreachable in simulation */
	}

	gpio_init_callback(&limit_lower_cb_data[lane], limit_lower_isr,
			   BIT(limit_lower[lane].pin));
	return gpio_add_callback(limit_lower[lane].port,
				 &limit_lower_cb_data[lane]);
}

int hw_register_beam_cb(lane_id_t lane, hw_exti_cb_t cb, void *data)
{
	beam_user_cb[lane] = cb;
	beam_user_data[lane] = data;

	int rc = gpio_pin_configure_dt(&beam_pin[lane], GPIO_INPUT);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: driver error unreachable in simulation */
	}
	rc = gpio_pin_interrupt_configure_dt(&beam_pin[lane],
					     GPIO_INT_EDGE_BOTH);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: driver error unreachable in simulation */
	}

	gpio_init_callback(&beam_cb_data[lane], beam_isr,
			   BIT(beam_pin[lane].pin));
	return gpio_add_callback(beam_pin[lane].port, &beam_cb_data[lane]);
}

/* ── UART implementation ─────────────────────────────────────────── */

int hw_uart_camera_init(lane_id_t lane, hw_uart_rx_cb_t cb, void *data)
{
	if (!device_is_ready(camera_dev[lane])) {
		return -ENODEV; /* LCOV_EXCL_LINE: device always ready in simulation */
	}
	camera_rx_user_cb[lane] = cb;
	camera_rx_user_data[lane] = data;

	uart_irq_callback_user_data_set(camera_dev[lane], camera_uart_isr,
					(void *)(uintptr_t)lane);
	uart_irq_rx_enable(camera_dev[lane]);
	return 0;
}

int hw_uart_camera_send(lane_id_t lane, const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(camera_dev[lane], data[i]);
	}
	return 0;
}

int hw_uart_payment_init(hw_uart_rx_cb_t cb, void *data)
{
	if (!device_is_ready(payment_dev)) {
		return -ENODEV; /* LCOV_EXCL_LINE: device always ready in simulation */
	}
	payment_rx_user_cb = cb;
	payment_rx_user_data = data;

	uart_irq_callback_user_data_set(payment_dev, payment_uart_isr, NULL);
	uart_irq_rx_enable(payment_dev);
	return 0;
}

int hw_uart_payment_send(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(payment_dev, data[i]);
	}
	return 0;
}

/* ── ADC implementation ──────────────────────────────────────────── */

int hw_adc_init(void)
{
	if (!device_is_ready(adc_dev)) {
		return -ENODEV; /* LCOV_EXCL_LINE: device always ready in simulation */
	}

	for (int i = 0; i < LANE_COUNT; i++) {
		int rc = adc_channel_setup(adc_dev, &adc_ch_cfg[i]);
		if (rc != 0) {
			return rc; /* LCOV_EXCL_LINE: channel setup always succeeds */
		}

		k_poll_signal_init(&adc_signal[i]);

		adc_seq[i].channels = BIT(adc_ch_cfg[i].channel_id);
		adc_seq[i].buffer = &adc_sample_buf[i];
		adc_seq[i].buffer_size = sizeof(adc_sample_buf[i]);
		adc_seq[i].resolution = 12;
		adc_seq[i].options = &adc_opts[i];
		adc_opts[i].extra_samplings = 0;

		adc_busy[i] = false;
		adc_ready[i] = false;
	}
	return 0;
}

int hw_adc_read_async(lane_id_t lane)
{
	if (adc_busy[lane]) {
		return -EBUSY;
	}

	k_poll_signal_reset(&adc_signal[lane]);
	adc_busy[lane] = true;
	adc_ready[lane] = false;

	int rc = adc_read_async(adc_dev, &adc_seq[lane], &adc_signal[lane]);
	if (rc != 0) {
		adc_busy[lane] = false;
		return rc;
	}
	return 0;
}

int hw_adc_check_completion(lane_id_t lane, uint16_t *raw_code)
{
	if (!adc_busy[lane]) {
		return -EINVAL;
	}

	unsigned int signaled;
	int result;

	k_poll_signal_check(&adc_signal[lane], &signaled, &result);
	if (!signaled) {
		return -EAGAIN;
	}

	adc_busy[lane] = false;
	adc_ready[lane] = true;

	if (result != 0) {
		return result;
	}

	int16_t raw = adc_sample_buf[lane];
	if (raw < 0) {
		raw = 0;
	}
	if (raw > 4095) {
		raw = 4095;
	}
	*raw_code = (uint16_t)raw;
	return 0;
}

/* ── SPI display implementation ──────────────────────────────────── */

int hw_display_init(void)
{
	if (!device_is_ready(spi_dev)) {
		return -ENODEV; /* LCOV_EXCL_LINE: device always ready in simulation */
	}

	int rc = gpio_pin_configure_dt(&display_dc, GPIO_OUTPUT_INACTIVE);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: gpio config always succeeds in simulation */
	}

	rc = gpio_pin_configure_dt(&display_reset_pin, GPIO_OUTPUT_ACTIVE);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: gpio config always succeeds in simulation */
	}

	spi_cfg.cs = spi_cs;

	return 0;
}

int hw_display_reset(void)
{
	/* Assert reset (active low, so set to 0 = active) */
	int rc = gpio_pin_set_dt(&display_reset_pin, 1);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: gpio always succeeds */
	}
	k_msleep(10);
	/* De-assert reset */
	rc = gpio_pin_set_dt(&display_reset_pin, 0);
	if (rc != 0) {
		return rc; /* LCOV_EXCL_LINE: gpio always succeeds */
	}
	k_msleep(120);
	return 0;
}

int hw_display_write_cmd(uint8_t cmd)
{
	gpio_pin_set_dt(&display_dc, 0); /* Command mode */

	struct spi_buf buf = { .buf = &cmd, .len = 1 };
	struct spi_buf_set tx = { .buffers = &buf, .count = 1 };

	return spi_write(spi_dev, &spi_cfg, &tx);
}

int hw_display_write_data(const uint8_t *data, size_t len)
{
	gpio_pin_set_dt(&display_dc, 1); /* Data mode */

	struct spi_buf buf = { .buf = (void *)data, .len = len };
	struct spi_buf_set tx = { .buffers = &buf, .count = 1 };

	return spi_write(spi_dev, &spi_cfg, &tx);
}

int hw_display_write_pixels(const uint8_t *data, size_t len)
{
	return hw_display_write_data(data, len);
}

/* ── Full hardware init ──────────────────────────────────────────── */

static int hw_init_gpio_outputs(void)
{
	int rc;
	for (int i = 0; i < LANE_COUNT; i++) {
		rc = gpio_pin_configure_dt(&motor_enable[i], GPIO_OUTPUT_INACTIVE);
		if (rc != 0) {
			return rc; /* LCOV_EXCL_LINE: gpio config always succeeds */
		}
		rc = gpio_pin_configure_dt(&motor_direction[i], GPIO_OUTPUT_INACTIVE);
		if (rc != 0) {
			return rc; /* LCOV_EXCL_LINE: gpio config always succeeds */
		}
		rc = gpio_pin_configure_dt(&light_red[i], GPIO_OUTPUT_ACTIVE);
		if (rc != 0) {
			return rc; /* LCOV_EXCL_LINE: gpio config always succeeds */
		}
		rc = gpio_pin_configure_dt(&light_green[i], GPIO_OUTPUT_INACTIVE);
		if (rc != 0) {
			return rc; /* LCOV_EXCL_LINE: gpio config always succeeds */
		}
		rc = gpio_pin_configure_dt(&buzzer_pin[i], GPIO_OUTPUT_INACTIVE);
		if (rc != 0) {
			return rc; /* LCOV_EXCL_LINE: gpio config always succeeds */
		}
	}
	return 0;
}

int hw_init(void)
{
	int rc = hw_init_gpio_outputs();
	if (rc != 0) {
		return rc;
	}

	rc = hw_adc_init();
	if (rc != 0) {
		return rc;
	}

	rc = hw_display_init();
	if (rc != 0) {
		return rc;
	}

	return 0;
}
