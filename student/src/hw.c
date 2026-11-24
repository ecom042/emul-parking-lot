/*
 * Camada fina de hardware: drivers reais do Zephyr. Veja hw.h.
 *
 * Interrupções:
 *  - fins de curso e feixe (EXTI, borda de subida e de descida) e bytes
 *    recebidos nas UARTs só ACORDAM o laço de controle (k_sem_give). Quem
 *    decide e mexe nos pinos é o laço, em contexto de thread, que tem
 *    prioridade maior que a thread do display; a reação leva microssegundos
 *    de tempo virtual, bem abaixo dos 10 ms exigidos para a obstrução;
 *  - a conclusão do ADC sinaliza um k_poll_signal, verificado sem bloquear.
 */
#include "hw.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

/* ---- Pinos de cada faixa (aliases do devicetree, nunca números) --------- */

struct lane_pins {
	struct gpio_dt_spec enable, direction, red, green, buzzer;
	struct gpio_dt_spec upper, lower, beam;
};

#define LANE_PINS(prefix)                                                                  \
	{                                                                                  \
		.enable = GPIO_DT_SPEC_GET(DT_ALIAS(prefix##_motor_enable), gpios),        \
		.direction = GPIO_DT_SPEC_GET(DT_ALIAS(prefix##_motor_direction), gpios),  \
		.red = GPIO_DT_SPEC_GET(DT_ALIAS(prefix##_light_red), gpios),              \
		.green = GPIO_DT_SPEC_GET(DT_ALIAS(prefix##_light_green), gpios),          \
		.buzzer = GPIO_DT_SPEC_GET(DT_ALIAS(prefix##_buzzer), gpios),              \
		.upper = GPIO_DT_SPEC_GET(DT_ALIAS(prefix##_limit_upper), gpios),          \
		.lower = GPIO_DT_SPEC_GET(DT_ALIAS(prefix##_limit_lower), gpios),          \
		.beam = GPIO_DT_SPEC_GET(DT_ALIAS(prefix##_beam), gpios),                  \
	}

static const struct lane_pins pins[LANE_COUNT] = {
	[LANE_ENTRY] = LANE_PINS(entry),
	[LANE_EXIT] = LANE_PINS(exit),
};

/* ---- ADC4: um dispositivo, dois canais ---------------------------------- */

/*
 * O alias aponta para o nó do canal (channel@11 = IN17, channel@12 = IN18);
 * o pai é o ADC4. A configuração do canal (ganho 1, referência interna,
 * 12 bits) vem do devicetree.
 */
#define LANE_ADC(alias)                                                                    \
	{                                                                                  \
		.dev = DEVICE_DT_GET(DT_PARENT(DT_ALIAS(alias))),                          \
		.channel_id = DT_REG_ADDR(DT_ALIAS(alias)),                                \
		.channel_cfg_dt_node_exists = true,                                        \
		.channel_cfg = ADC_CHANNEL_CFG_DT(DT_ALIAS(alias)),                        \
		.resolution = DT_PROP(DT_ALIAS(alias), zephyr_resolution),                 \
	}

static const struct adc_dt_spec adc_channels[LANE_COUNT] = {
	[LANE_ENTRY] = LANE_ADC(entry_distance),
	[LANE_EXIT] = LANE_ADC(exit_distance),
};

/*
 * REQ-10: buffer, sequência e sinal são ESTÁTICOS. O driver guarda ponteiros
 * para eles até a conversão terminar, mesmo que termine depois do nosso
 * prazo; por isso não podem estar na pilha de uma função.
 */
static uint16_t adc_buffer;
static struct adc_sequence adc_seq = {
	.buffer = &adc_buffer,
	.buffer_size = sizeof(adc_buffer),
	.resolution = 12,
};
static struct k_poll_signal adc_signal;

/* ---- UARTs de equipamento ----------------------------------------------- */

static const struct device *const uarts[UART_COUNT] = {
	[UART_CAM_ENTRY] = DEVICE_DT_GET(DT_ALIAS(entry_camera)),
	[UART_CAM_EXIT] = DEVICE_DT_GET(DT_ALIAS(exit_camera)),
	[UART_PAYMENT] = DEVICE_DT_GET(DT_ALIAS(payment_terminal)),
};

/** Um anel de recepção por UART, preenchido pela interrupção. */
RING_BUF_DECLARE(rx_entry, 256);
RING_BUF_DECLARE(rx_exit, 256);
RING_BUF_DECLARE(rx_payment, 256);
static struct ring_buf *const rx_rings[UART_COUNT] = {&rx_entry, &rx_exit, &rx_payment};
/* Índices passados como user_data para a ISR saber qual anel usar. */
static enum uart_ch uart_ids[UART_COUNT] = {UART_CAM_ENTRY, UART_CAM_EXIT,
						  UART_PAYMENT};

/* ---- Display ------------------------------------------------------------ */

#define DISPLAY_NODE DT_ALIAS(payment_display)
static const struct spi_dt_spec disp_spi =
	SPI_DT_SPEC_GET(DISPLAY_NODE, SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_CONTROLLER);
static const struct gpio_dt_spec disp_dc = GPIO_DT_SPEC_GET(DISPLAY_NODE, dc_gpios);
static const struct gpio_dt_spec disp_reset = GPIO_DT_SPEC_GET(DISPLAY_NODE, reset_gpios);

/* ---- Despertador do laço de controle ------------------------------------ */

K_SEM_DEFINE(wake_sem, 0, 1);
static struct gpio_callback input_cb[LANE_COUNT];

void hw_wake(void)
{
	k_sem_give(&wake_sem);
}

/** EXTI de fim de curso ou feixe: só acorda o laço. */
static void input_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins_mask)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins_mask);
	hw_wake();
}

/** RX de UART: copia os bytes para o anel daquela UART e acorda o laço. */
static void uart_isr(const struct device *dev, void *user_data)
{
	const enum uart_ch ch = *(const enum uart_ch *)user_data;
	uint8_t byte;

	uart_irq_update(dev);
	while (uart_irq_rx_ready(dev) > 0) {
		while (uart_fifo_read(dev, &byte, 1) == 1) {
			/* Anel cheio: o byte se perde e a linha sai malformada. */
			(void)ring_buf_put(rx_rings[ch], &byte, 1);
		}
	}
	hw_wake();
}

/* ---- Inicialização ------------------------------------------------------ */

/** Configura uma entrada com interrupção nas duas bordas. */
static int setup_input(const struct gpio_dt_spec *in)
{
	int rc = gpio_pin_configure_dt(in, GPIO_INPUT);

	rc |= gpio_pin_interrupt_configure_dt(in, GPIO_INT_EDGE_BOTH);
	return rc;
}

static int setup_lane(enum lane_id lane)
{
	const struct lane_pins *p = &pins[lane];
	int rc = 0;

	/* Saídas: tudo desligado, depois o vermelho (REQ-01). */
	rc |= gpio_pin_configure_dt(&p->enable, GPIO_OUTPUT_INACTIVE);
	rc |= gpio_pin_configure_dt(&p->direction, GPIO_OUTPUT_INACTIVE);
	rc |= gpio_pin_configure_dt(&p->green, GPIO_OUTPUT_INACTIVE);
	rc |= gpio_pin_configure_dt(&p->buzzer, GPIO_OUTPUT_INACTIVE);
	rc |= gpio_pin_configure_dt(&p->red, GPIO_OUTPUT_ACTIVE);

	/* Entradas no mesmo porto (C): um callback por faixa com as 3 linhas. */
	rc |= setup_input(&p->upper);
	rc |= setup_input(&p->lower);
	rc |= setup_input(&p->beam);
	gpio_init_callback(&input_cb[lane], input_isr,
			   BIT(p->upper.pin) | BIT(p->lower.pin) | BIT(p->beam.pin));
	rc |= gpio_add_callback(p->upper.port, &input_cb[lane]);

	rc |= adc_channel_setup_dt(&adc_channels[lane]);
	return rc;
}

int hw_init(void)
{
	int rc = 0;

	/*
	 * Os erros são acumulados com | (bit a bit) em vez de um if por chamada:
	 * qualquer falha deixa rc != 0, e main.c para o firmware com o vermelho
	 * aceso. Nenhuma dessas chamadas falha no laboratório.
	 */
	rc |= device_is_ready(adc_channels[LANE_ENTRY].dev) ? 0 : -ENODEV; /* LCOV_EXCL_LINE: o ADC4 sempre fica pronto no Renode */
	rc |= setup_lane(LANE_ENTRY);
	rc |= setup_lane(LANE_EXIT);
	k_poll_signal_init(&adc_signal);

	for (int ch = 0; ch < UART_COUNT; ch++) {
		rc |= device_is_ready(uarts[ch]) ? 0 : -ENODEV; /* LCOV_EXCL_LINE: as UARTs sempre ficam prontas no Renode */
		rc |= uart_irq_callback_user_data_set(uarts[ch], uart_isr, &uart_ids[ch]);
		uart_irq_rx_enable(uarts[ch]);
	}

	rc |= spi_is_ready_dt(&disp_spi) ? 0 : -ENODEV; /* LCOV_EXCL_LINE: o SPI1 sempre fica pronto no Renode */
	rc |= gpio_pin_configure_dt(&disp_dc, GPIO_OUTPUT_INACTIVE);
	rc |= gpio_pin_configure_dt(&disp_reset, GPIO_OUTPUT_INACTIVE);
	return rc;
}

/* ---- GPIO --------------------------------------------------------------- */

int hw_motor(enum lane_id lane, enum motor_cmd cmd)
{
	const struct lane_pins *p = &pins[lane];

	if (cmd == MOTOR_OFF) {
		return gpio_pin_set_dt(&p->enable, 0);
	}
	/* Direção primeiro, depois o enable: o motor nunca gira para o lado errado. */
	int rc = gpio_pin_set_dt(&p->direction, cmd == MOTOR_OPEN ? 1 : 0);

	rc |= gpio_pin_set_dt(&p->enable, 1);
	return rc;
}

int hw_lights(enum lane_id lane, bool red, bool green)
{
	const struct lane_pins *p = &pins[lane];
	/* Apaga antes de acender: os dois nunca ficam acesos ao mesmo tempo. */
	int rc = gpio_pin_set_dt(red ? &p->green : &p->red, 0);

	rc |= gpio_pin_set_dt(&p->red, red ? 1 : 0);
	rc |= gpio_pin_set_dt(&p->green, green ? 1 : 0);
	return rc;
}

int hw_buzzer(enum lane_id lane, bool on)
{
	return gpio_pin_set_dt(&pins[lane].buzzer, on ? 1 : 0);
}

bool hw_limit_upper(enum lane_id lane)
{
	return gpio_pin_get_dt(&pins[lane].upper) > 0;
}

bool hw_limit_lower(enum lane_id lane)
{
	return gpio_pin_get_dt(&pins[lane].lower) > 0;
}

bool hw_beam(enum lane_id lane)
{
	return gpio_pin_get_dt(&pins[lane].beam) > 0;
}

/* ---- UART --------------------------------------------------------------- */

int hw_uart_send(enum uart_ch ch, const char *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uarts[ch], (unsigned char)buf[i]);
	}
	return 0;
}

size_t hw_uart_read(enum uart_ch ch, uint8_t *buf, size_t max)
{
	/* A ISR escreve e o laço lê: o ring_buf aguenta um produtor e um consumidor. */
	return ring_buf_get(rx_rings[ch], buf, (uint32_t)max);
}

/* ---- ADC4 --------------------------------------------------------------- */

int hw_adc_start(enum lane_id lane)
{
	adc_seq.channels = BIT(adc_channels[lane].channel_id);
	k_poll_signal_reset(&adc_signal);
	return adc_read_async(adc_channels[lane].dev, &adc_seq, &adc_signal);
}

int hw_adc_check(uint16_t *raw)
{
	unsigned int signaled = 0;
	int result = 0;

	k_poll_signal_check(&adc_signal, &signaled, &result);
	if (signaled == 0U) {
		return -EINPROGRESS;
	}
	*raw = adc_buffer;
	return result;
}

/* ---- Display ------------------------------------------------------------ */

int hw_disp_reset(bool asserted)
{
	/* reset-gpios é ativo baixo no devicetree: 1 = reset ativo. */
	return gpio_pin_set_dt(&disp_reset, asserted ? 1 : 0);
}

int hw_disp_write(bool data, uint8_t *buf, size_t len)
{
	const struct spi_buf b = {.buf = buf, .len = len};
	const struct spi_buf_set set = {.buffers = &b, .count = 1};
	int rc = gpio_pin_set_dt(&disp_dc, data ? 1 : 0);

	/* O chip select (PD14) é controlado pelo driver em cada transação. */
	rc |= spi_write_dt(&disp_spi, &set);
	return rc;
}

/* ---- Tempo -------------------------------------------------------------- */

int64_t hw_now_ms(void)
{
	return k_uptime_get();
}

void hw_sleep_ms(int32_t ms)
{
	k_msleep(ms);
}

void hw_wait_event(int32_t timeout_ms)
{
	(void)k_sem_take(&wake_sem, K_MSEC(timeout_ms));
}
