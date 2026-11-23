/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#include "fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(bool, fake_device_is_ready, const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, fake_uart_irq_callback_user_data_set,
		       const struct device *, uart_irq_callback_user_data_t, void *);
DEFINE_FAKE_VOID_FUNC(fake_uart_irq_rx_enable, const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, fake_uart_irq_update, const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, fake_uart_irq_rx_ready, const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, fake_uart_fifo_read, const struct device *, uint8_t *, int);
DEFINE_FAKE_VALUE_FUNC(int, fake_uart_irq_tx_ready, const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, fake_uart_fifo_fill, const struct device *, const uint8_t *, int);
DEFINE_FAKE_VOID_FUNC(fake_uart_irq_tx_enable, const struct device *);
DEFINE_FAKE_VOID_FUNC(fake_uart_irq_tx_disable, const struct device *);

uart_irq_callback_user_data_t fake_isr[FAKE_PORTS];
void *fake_isr_data[FAKE_PORTS];

const uint8_t *fake_rx_src;
int fake_rx_len;

uint8_t fake_tx_sink[256];
int fake_tx_sink_len;
int fake_tx_fill_max;

const struct device *const fake_port_devs[FAKE_PORTS] = {
	DEVICE_DT_GET(DT_ALIAS(entry_camera)),
	DEVICE_DT_GET(DT_ALIAS(exit_camera)),
	DEVICE_DT_GET(DT_ALIAS(payment_terminal)),
};

int fake_port_of(const struct device *dev)
{
	for (int i = 0; i < FAKE_PORTS; i++) {
		if (fake_port_devs[i] == dev) {
			return i;
		}
	}
	return -1;
}

/* Guarda a ISR e o user_data de cada porta, como o driver real faria. */
static int callback_set_custom(const struct device *dev, uart_irq_callback_user_data_t cb,
			       void *user_data)
{
	int i = fake_port_of(dev);

	if (i >= 0) {
		fake_isr[i] = cb;
		fake_isr_data[i] = user_data;
	}
	return 0;
}

/* Entrega ate len bytes de fake_rx_src, como um FIFO de recepcao. */
static int fifo_read_custom(const struct device *dev, uint8_t *buf, int len)
{
	int n = MIN(len, fake_rx_len);

	ARG_UNUSED(dev);
	if (n <= 0) {
		return 0;
	}
	memcpy(buf, fake_rx_src, (size_t)n);
	fake_rx_src += n;
	fake_rx_len -= n;
	return n;
}

/* Aceita ate fake_tx_fill_max bytes por chamada e os anota em fake_tx_sink. */
static int fifo_fill_custom(const struct device *dev, const uint8_t *buf, int len)
{
	int n = MIN(len, fake_tx_fill_max);

	ARG_UNUSED(dev);
	if (n <= 0) {
		return 0;
	}
	n = MIN(n, (int)sizeof(fake_tx_sink) - fake_tx_sink_len);
	memcpy(&fake_tx_sink[fake_tx_sink_len], buf, (size_t)n);
	fake_tx_sink_len += n;
	return n;
}

void fakes_reset(void)
{
	RESET_FAKE(fake_device_is_ready);
	RESET_FAKE(fake_uart_irq_callback_user_data_set);
	RESET_FAKE(fake_uart_irq_rx_enable);
	RESET_FAKE(fake_uart_irq_update);
	RESET_FAKE(fake_uart_irq_rx_ready);
	RESET_FAKE(fake_uart_fifo_read);
	RESET_FAKE(fake_uart_irq_tx_ready);
	RESET_FAKE(fake_uart_fifo_fill);
	RESET_FAKE(fake_uart_irq_tx_enable);
	RESET_FAKE(fake_uart_irq_tx_disable);
	FFF_RESET_HISTORY();

	memset(fake_isr, 0, sizeof(fake_isr));
	memset(fake_isr_data, 0, sizeof(fake_isr_data));
	fake_rx_src = NULL;
	fake_rx_len = 0;
	memset(fake_tx_sink, 0, sizeof(fake_tx_sink));
	fake_tx_sink_len = 0;
	fake_tx_fill_max = 256;

	fake_device_is_ready_fake.return_val = true;
	fake_uart_irq_callback_user_data_set_fake.custom_fake = callback_set_custom;
	fake_uart_fifo_read_fake.custom_fake = fifo_read_custom;
	fake_uart_fifo_fill_fake.custom_fake = fifo_fill_custom;
}
