/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Falsos do FFF para toda a camada de hardware usada por controller.c e
 * lane.c. Alem dos falsos, um "mundo" simples guarda o que os sensores e as
 * UARTs devolvem: os falsos de leitura (fins de curso, bordas do feixe, ADC4,
 * quadros da camera e do terminal) leem dele.
 */
#ifndef TEST_FAKES_H_
#define TEST_FAKES_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/fff.h>
#include <zephyr/sys/util.h>

#include "at_parser.h"
#include "hw.h"
#include "hw_adc.h"
#include "hw_uart.h"
#include "lane_cfg.h"
#include "p1_parser.h"
#include "st7789.h"

/* hw.h */
DECLARE_FAKE_VALUE_FUNC(int, hw_io_init);
DECLARE_FAKE_VALUE_FUNC(int, hw_motor_drive, const struct lane_cfg *, bool);
DECLARE_FAKE_VALUE_FUNC(int, hw_motor_stop, const struct lane_cfg *);
DECLARE_FAKE_VALUE_FUNC(bool, hw_motor_running, const struct lane_cfg *);
DECLARE_FAKE_VALUE_FUNC(int, hw_lights_set, const struct lane_cfg *, bool, bool);
DECLARE_FAKE_VALUE_FUNC(int, hw_buzzer_pulse, const struct lane_cfg *);
DECLARE_FAKE_VALUE_FUNC(int, hw_beam_read, const struct lane_cfg *, bool *);
DECLARE_FAKE_VALUE_FUNC(uint32_t, hw_beam_take_edges, const struct lane_cfg *);
DECLARE_FAKE_VALUE_FUNC(int, hw_limits_read, const struct lane_cfg *, bool *,
			bool *);

/* hw_adc.h */
DECLARE_FAKE_VALUE_FUNC(int, hw_adc_init);
DECLARE_FAKE_VALUE_FUNC(int, hw_adc_poll, int64_t, struct hw_adc_sample *);

/* hw_uart.h */
DECLARE_FAKE_VALUE_FUNC(int, hw_uart_init);
DECLARE_FAKE_VALUE_FUNC(int, hw_uart_send, enum hw_uart_port, const char *);
DECLARE_FAKE_VALUE_FUNC(bool, hw_uart_poll_plate, enum hw_uart_port,
			struct at_plate *);
DECLARE_FAKE_VALUE_FUNC(bool, hw_uart_poll_payment, struct p1_result *);

/* st7789.h */
DECLARE_FAKE_VALUE_FUNC(int, st7789_start);
DECLARE_FAKE_VALUE_FUNC(int, st7789_show_plate, const char *);
DECLARE_FAKE_VALUE_FUNC(int, st7789_show_charge, uint32_t);

/** Quadros que cabem na fila de cada porta do mundo falso. */
#define MUNDO_FILA 4

/** O que os sensores e as UARTs falsos devolvem. */
struct mundo {
	/* Fins de curso de cada faixa e o retorno de hw_limits_read. */
	bool upper[LANE_COUNT];
	bool lower[LANE_COUNT];
	int limits_rc[LANE_COUNT];

	/* Bordas do feixe; hw_beam_take_edges devolve e zera, como a ISR. */
	uint32_t edges[LANE_COUNT];

	/* Retorno de hw_adc_poll e a amostra entregue quando ele e 1. */
	int adc_rc;
	struct hw_adc_sample adc;

	/* Filas de +PLATE de cada camera e de resultados do terminal. */
	struct at_plate plates[HW_UART_PORT_COUNT][MUNDO_FILA];
	uint8_t n_plates[HW_UART_PORT_COUNT];
	struct p1_result payments[MUNDO_FILA];
	uint8_t n_payments;

	/* Ultima linha enviada por hw_uart_send (o argumento e da pilha). */
	enum hw_uart_port uart_port;
	char uart_line[FRAME_MAX];
};

extern struct mundo mundo;

/** Mundo parado: as duas cancelas fechadas, feixe livre, nada nas UARTs. */
void mundo_reset(void);

/** Poe um +PLATE na fila da camera @p port. */
void mundo_placa(enum hw_uart_port port, uint32_t id, const char *plate,
		 uint8_t confidence);

/** Poe um resultado na fila do terminal. */
void mundo_pagamento(enum p1_status status, const char *uuid, uint32_t cents);

/**
 * Volta todos os falsos ao padrao (sucesso, motor parado) e liga os falsos
 * de leitura ao mundo. Nao mexe no mundo.
 */
void fakes_reset(void);

#endif /* TEST_FAKES_H_ */
