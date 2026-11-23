/* SPDX-License-Identifier: Apache-2.0 */
/* Falsos do FFF para as chamadas de driver da UART feitas pelo hw_uart.c. */
#ifndef TEST_HW_UART_FAKES_H_
#define TEST_HW_UART_FAKES_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fff.h>

DECLARE_FAKE_VALUE_FUNC(bool, fake_device_is_ready, const struct device *);
DECLARE_FAKE_VALUE_FUNC(int, fake_uart_irq_callback_user_data_set,
			const struct device *, uart_irq_callback_user_data_t, void *);
DECLARE_FAKE_VOID_FUNC(fake_uart_irq_rx_enable, const struct device *);
DECLARE_FAKE_VALUE_FUNC(int, fake_uart_irq_update, const struct device *);
DECLARE_FAKE_VALUE_FUNC(int, fake_uart_irq_rx_ready, const struct device *);
DECLARE_FAKE_VALUE_FUNC(int, fake_uart_fifo_read, const struct device *, uint8_t *, int);
DECLARE_FAKE_VALUE_FUNC(int, fake_uart_irq_tx_ready, const struct device *);
DECLARE_FAKE_VALUE_FUNC(int, fake_uart_fifo_fill, const struct device *, const uint8_t *, int);
DECLARE_FAKE_VOID_FUNC(fake_uart_irq_tx_enable, const struct device *);
DECLARE_FAKE_VOID_FUNC(fake_uart_irq_tx_disable, const struct device *);

/** Numero de portas acompanhadas pelos falsos (camera E, camera S, terminal). */
#define FAKE_PORTS 3

/** Dispositivos das portas, na ordem de enum hw_uart_port. */
extern const struct device *const fake_port_devs[FAKE_PORTS];

/** ISR e user_data registrados por hw_uart_init, por porta. */
extern uart_irq_callback_user_data_t fake_isr[FAKE_PORTS];
extern void *fake_isr_data[FAKE_PORTS];

/** Bytes que o "FIFO de recepcao" entrega a uart_fifo_read. */
extern const uint8_t *fake_rx_src;
extern int fake_rx_len;

/** Bytes aceitos por uart_fifo_fill, em ordem, e limite por chamada. */
extern uint8_t fake_tx_sink[256];
extern int fake_tx_sink_len;
extern int fake_tx_fill_max;

/** Indice da porta (0..2) do dispositivo, ou -1 se nao for de nenhuma. */
int fake_port_of(const struct device *dev);

/** Volta todos os falsos ao padrao: dispositivos prontos, sem dados. */
void fakes_reset(void);

#endif /* TEST_HW_UART_FAKES_H_ */
