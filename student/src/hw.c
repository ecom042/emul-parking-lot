/**
 * @file hw.c
 * @brief Camada fina de hardware sobre os drivers do Zephyr.
 */

#include "hw.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

/** @brief Saídas de uma faixa, na ordem de enum hw_output. */
#define LANE_OUTPUTS(lane)                                                                         \
	{                                                                                          \
		GPIO_DT_SPEC_GET(DT_ALIAS(lane##_motor_enable), gpios),                            \
		GPIO_DT_SPEC_GET(DT_ALIAS(lane##_motor_direction), gpios),                         \
		GPIO_DT_SPEC_GET(DT_ALIAS(lane##_light_red), gpios),                               \
		GPIO_DT_SPEC_GET(DT_ALIAS(lane##_light_green), gpios),                             \
		GPIO_DT_SPEC_GET(DT_ALIAS(lane##_buzzer), gpios),                                  \
	}

/** @brief Entradas de uma faixa, na ordem de enum hw_input. */
#define LANE_INPUTS(lane)                                                                          \
	{                                                                                          \
		{.spec = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_limit_upper), gpios)},                   \
		{.spec = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_limit_lower), gpios)},                   \
		{.spec = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_beam), gpios)},                          \
	}

#define DISPLAY_NODE DT_ALIAS(payment_display)

/** @brief Uma entrada digital e o callback da sua interrupção. */
struct input_line {
	struct gpio_dt_spec spec; /**< Pino. */
	struct gpio_callback cb;  /**< Registro no driver de GPIO. */
	size_t pending_bit;       /**< Índice da entrada no mapa de bordas. */
};

static const struct gpio_dt_spec outputs[HW_LANE_COUNT][HW_OUT_COUNT] = {
	LANE_OUTPUTS(entry),
	LANE_OUTPUTS(exit),
};

static struct input_line inputs[HW_LANE_COUNT][HW_IN_COUNT] = {
	LANE_INPUTS(entry),
	LANE_INPUTS(exit),
};

static const struct device *const uarts[HW_UART_COUNT] = {
	DEVICE_DT_GET(DT_ALIAS(entry_camera)),
	DEVICE_DT_GET(DT_ALIAS(exit_camera)),
	DEVICE_DT_GET(DT_ALIAS(payment_terminal)),
};

static const struct device *const adc_dev = DEVICE_DT_GET(DT_PARENT(DT_ALIAS(entry_distance)));

static const struct adc_channel_cfg adc_channels[HW_LANE_COUNT] = {
	ADC_CHANNEL_CFG_DT(DT_ALIAS(entry_distance)),
	ADC_CHANNEL_CFG_DT(DT_ALIAS(exit_distance)),
};

/*
 * Buffer, sequência e sinal são estáticos: o driver pode concluir a conversão
 * a qualquer momento depois de adc_read_async(), mesmo tarde.
 */
static uint16_t adc_sample;
static struct k_poll_signal adc_signal;
static struct adc_sequence adc_seq = {
	.buffer = &adc_sample,
	.buffer_size = sizeof(adc_sample),
	.resolution = DT_PROP(DT_ALIAS(entry_distance), zephyr_resolution),
};

static const struct spi_dt_spec display_spi =
	SPI_DT_SPEC_GET(DISPLAY_NODE, SPI_OP_MODE_CONTROLLER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB);
static const struct gpio_dt_spec display_dc = GPIO_DT_SPEC_GET(DISPLAY_NODE, dc_gpios);
static const struct gpio_dt_spec display_rst = GPIO_DT_SPEC_GET(DISPLAY_NODE, reset_gpios);

static ATOMIC_DEFINE(input_pending, HW_LANE_COUNT * HW_IN_COUNT);
static hw_input_cb_t input_cb;

/** @brief Entrega à lógica, em contexto de work queue, as bordas anotadas na ISR. */
static void input_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	for (size_t i = 0; i < HW_LANE_COUNT * HW_IN_COUNT; i++) {
		if (atomic_test_and_clear_bit(input_pending, i) && input_cb != NULL) {
			input_cb(i / HW_IN_COUNT, i % HW_IN_COUNT);
		}
	}
}

static K_WORK_DEFINE(input_work, input_work_handler);

/** @brief ISR de borda: só anota a entrada e adia o tratamento para a work queue. */
static void input_isr(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
	struct input_line *line = CONTAINER_OF(cb, struct input_line, cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	atomic_set_bit(input_pending, line->pending_bit);
	k_work_submit(&input_work);
}

/**
 * @brief Mantém o primeiro erro de uma sequência de passos de inicialização.
 *
 * Os passos seguintes rodam mesmo depois de um erro: a inicialização é tudo ou
 * nada e o erro devolvido é o primeiro.
 */
static int first_error(int err, int next)
{
	return err < 0 ? err : next; /* LCOV_EXCL_LINE: drivers do STM32 não falham no modelo */
}

/** @brief Converte a prontidão de um dispositivo em errno. */
static int ready(const struct device *dev)
{
	return device_is_ready(dev) ? 0 : -ENODEV; /* LCOV_EXCL_LINE: todos ficam prontos */
}

/** @brief Configura um pino de saída, já no nível pedido. */
static int output_setup(const struct gpio_dt_spec *spec, bool on)
{
	return first_error(ready(spec->port),
			   gpio_pin_configure_dt(spec, on ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE));
}

/** @brief Configura uma entrada com interrupção nas duas bordas. */
static int input_setup(struct input_line *line)
{
	int err = first_error(ready(line->spec.port), gpio_pin_configure_dt(&line->spec, GPIO_INPUT));

	gpio_init_callback(&line->cb, input_isr, BIT(line->spec.pin));
	err = first_error(err, gpio_add_callback_dt(&line->spec, &line->cb));
	return first_error(err, gpio_pin_interrupt_configure_dt(&line->spec, GPIO_INT_EDGE_BOTH));
}

int hw_init(void)
{
	int err = 0;

	for (size_t lane = 0; lane < HW_LANE_COUNT; lane++) {
		for (size_t out = 0; out < HW_OUT_COUNT; out++) {
			err = first_error(err, output_setup(&outputs[lane][out], out == HW_OUT_RED));
		}
		for (size_t in = 0; in < HW_IN_COUNT; in++) {
			inputs[lane][in].pending_bit = lane * HW_IN_COUNT + in;
			err = first_error(err, input_setup(&inputs[lane][in]));
		}
		err = first_error(err, ready(uarts[lane]));
		err = first_error(err, ready(adc_dev));
		err = first_error(err, adc_channel_setup(adc_dev, &adc_channels[lane]));
	}
	k_poll_signal_init(&adc_signal);
	err = first_error(err, ready(uarts[HW_UART_PAYMENT]));
	err = first_error(err, ready(display_spi.bus));
	err = first_error(err, output_setup(&display_dc, false));
	return first_error(err, output_setup(&display_rst, false));
}

void hw_set_input_callback(hw_input_cb_t cb)
{
	input_cb = cb;
}

int hw_output_set(enum hw_lane lane, enum hw_output output, bool on)
{
	return gpio_pin_set_dt(&outputs[lane][output], on);
}

int hw_input_get(enum hw_lane lane, enum hw_input input)
{
	return gpio_pin_get_dt(&inputs[lane][input].spec);
}

int hw_uart_write(enum hw_uart uart, const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uarts[uart], data[i]);
	}
	return 0;
}

int hw_uart_read(enum hw_uart uart, uint8_t *byte)
{
	return uart_poll_in(uarts[uart], byte) == 0 ? 0 : -EAGAIN;
}

int hw_adc_start(enum hw_lane lane)
{
	adc_seq.channels = BIT(adc_channels[lane].channel_id);
	k_poll_signal_reset(&adc_signal);
	return adc_read_async(adc_dev, &adc_seq, &adc_signal);
}

int hw_adc_poll(uint16_t *raw)
{
	unsigned int signaled;
	int result;

	k_poll_signal_check(&adc_signal, &signaled, &result);
	if (signaled == 0U) {
		return -EAGAIN;
	}
	*raw = adc_sample;
	return result;
}

int hw_display_reset(bool asserted)
{
	return gpio_pin_set_dt(&display_rst, asserted);
}

/** @brief Escreve bytes no SPI do display com D/C no nível pedido. */
static int display_write(bool data, const uint8_t *bytes, size_t len)
{
	/* spi_buf não tem versão const; numa escrita o driver só lê o buffer. */
	const struct spi_buf buf = {.buf = (void *)(uintptr_t)bytes, .len = len};
	const struct spi_buf_set set = {.buffers = &buf, .count = 1};
	int err = gpio_pin_set_dt(&display_dc, data);

	return err == 0 ? spi_write_dt(&display_spi, &set) : err;
}

int hw_display_cmd(uint8_t cmd, const uint8_t *params, size_t len)
{
	int err = display_write(false, &cmd, 1);

	return err == 0 && len > 0 ? display_write(true, params, len) : err;
}

int hw_display_data(const uint8_t *data, size_t len)
{
	return display_write(true, data, len);
}
