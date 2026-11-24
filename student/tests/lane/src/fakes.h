/* SPDX-License-Identifier: Apache-2.0 */
/* Falsos do FFF para a camada de hardware usada pela logica das faixas. */
#ifndef TEST_FAKES_H_
#define TEST_FAKES_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/fff.h>
#include <zephyr/sys/util.h>

#include "hw.h"
#include "hw_uart.h"
#include "lane_cfg.h"
#include "st7789.h"

DECLARE_FAKE_VALUE_FUNC(int, hw_motor_drive, const struct lane_cfg *, bool);
DECLARE_FAKE_VALUE_FUNC(int, hw_motor_stop, const struct lane_cfg *);
DECLARE_FAKE_VALUE_FUNC(bool, hw_motor_running, const struct lane_cfg *);
DECLARE_FAKE_VALUE_FUNC(int, hw_lights_set, const struct lane_cfg *, bool, bool);
DECLARE_FAKE_VALUE_FUNC(int, hw_buzzer_pulse, const struct lane_cfg *);
DECLARE_FAKE_VALUE_FUNC(int, hw_uart_send, enum hw_uart_port, const char *);
DECLARE_FAKE_VALUE_FUNC(int, st7789_show_plate, const char *, uint32_t *);
DECLARE_FAKE_VALUE_FUNC(int, st7789_show_charge, uint32_t, uint32_t *);

/** Ultima linha enviada por hw_uart_send (o argumento e um buffer da pilha). */
extern char fake_uart_line[64];

/** Ultimo numero de pedido entregue pelos falsos do display. */
extern uint32_t fake_ticket;

/** Valores devolvidos por st7789_show_plate e st7789_show_charge. */
extern int fake_plate_rc, fake_charge_rc;

/** Valor devolvido por hw_uart_send. */
extern int fake_uart_rc;

/** Volta todos os falsos ao padrao: sucesso, motor parado. */
void fakes_reset(void);

#endif /* TEST_FAKES_H_ */
