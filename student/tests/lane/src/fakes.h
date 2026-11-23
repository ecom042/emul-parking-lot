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
DECLARE_FAKE_VALUE_FUNC(int, hw_beam_read, const struct lane_cfg *, bool *);
DECLARE_FAKE_VALUE_FUNC(int, hw_limits_read, const struct lane_cfg *, bool *, bool *);
DECLARE_FAKE_VALUE_FUNC(int, hw_uart_send, enum hw_uart_port, const char *);
DECLARE_FAKE_VALUE_FUNC(int, st7789_show_plate, const char *);
DECLARE_FAKE_VALUE_FUNC(int, st7789_show_charge, uint32_t);

/** Niveis devolvidos por hw_limits_read e hw_beam_read nos falsos. */
extern bool fake_upper, fake_lower, fake_beam;

/** Volta todos os falsos ao padrao: sucesso, cancela fechada, feixe livre. */
void fakes_reset(void);

#endif /* TEST_FAKES_H_ */
