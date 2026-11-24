/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Header forcado (-include) so no hw_uart.c desta suite. Primeiro inclui os
 * headers reais que o hw_uart.c inclui; o include guard faz com que as
 * inclusoes do .c nao os reprocessem. Depois redireciona as chamadas de
 * driver da UART para falsos do FFF, que o teste controla. O kernel (ring
 * buffer, k_uptime_get) continua o real.
 */
#ifndef TEST_HW_UART_SHIM_H_
#define TEST_HW_UART_SHIM_H_

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include "fakes.h"

#define device_is_ready			fake_device_is_ready
#define uart_irq_callback_user_data_set	fake_uart_irq_callback_user_data_set
#define uart_irq_rx_enable		fake_uart_irq_rx_enable
#define uart_irq_update			fake_uart_irq_update
#define uart_irq_rx_ready		fake_uart_irq_rx_ready
#define uart_fifo_read			fake_uart_fifo_read
#define uart_irq_tx_ready		fake_uart_irq_tx_ready
#define uart_fifo_fill			fake_uart_fifo_fill
#define uart_irq_tx_enable		fake_uart_irq_tx_enable
#define uart_irq_tx_disable		fake_uart_irq_tx_disable

#endif /* TEST_HW_UART_SHIM_H_ */
