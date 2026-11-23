/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Falsos do FFF para as funcoes gpio_*_dt usadas por hw_io.c, e um modelo
 * simples dos 16 pinos das duas faixas: cada pino guarda o ultimo nivel
 * escrito (saidas) ou o nivel a ler (entradas), e pode receber um erro por
 * operacao.
 */
#ifndef TEST_HW_IO_FAKES_H_
#define TEST_HW_IO_FAKES_H_

#include <stdbool.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/fff.h>

#include "lane_cfg.h"

DECLARE_FAKE_VALUE_FUNC(bool, fake_gpio_is_ready_dt, const struct gpio_dt_spec *);
DECLARE_FAKE_VALUE_FUNC(int, fake_gpio_pin_configure_dt, const struct gpio_dt_spec *,
			gpio_flags_t);
DECLARE_FAKE_VALUE_FUNC(int, fake_gpio_pin_set_dt, const struct gpio_dt_spec *, int);
DECLARE_FAKE_VALUE_FUNC(int, fake_gpio_pin_get_dt, const struct gpio_dt_spec *);
DECLARE_FAKE_VALUE_FUNC(int, fake_gpio_pin_interrupt_configure_dt,
			const struct gpio_dt_spec *, gpio_flags_t);
DECLARE_FAKE_VALUE_FUNC(int, fake_gpio_add_callback_dt, const struct gpio_dt_spec *,
			struct gpio_callback *);

/** Pinos de uma faixa, na mesma nomenclatura de hw_io.c. */
enum pino {
	P_ENABLE,
	P_DIRECTION,
	P_RED,
	P_GREEN,
	P_BUZZER,
	P_UPPER,
	P_LOWER,
	P_BEAM,
	P_COUNT,
};

/** Operacoes de driver que podem falhar em um pino. */
enum op {
	OP_READY,  /* gpio_is_ready_dt devolve false */
	OP_CONFIG, /* gpio_pin_configure_dt */
	OP_SET,    /* gpio_pin_set_dt */
	OP_GET,    /* gpio_pin_get_dt */
	OP_IRQ,    /* gpio_pin_interrupt_configure_dt */
	OP_CB,     /* gpio_add_callback_dt */
	OP_COUNT,
};

/** Specs de referencia, montados pelos mesmos aliases do devicetree. */
extern const struct gpio_dt_spec pinos_ref[LANE_COUNT][P_COUNT];

/** Nivel de cada pino: ultimo escrito (saida) ou o que sera lido (entrada). */
extern int pino_nivel[LANE_COUNT][P_COUNT];

/** Callback registrado por gpio_add_callback_dt em cada pino (ou NULL). */
extern struct gpio_callback *pino_cb[LANE_COUNT][P_COUNT];

/** Faz a operacao @p o falhar sempre com @p rc no pino dado (rc 0 desfaz). */
void pino_falha(enum op o, enum lane_id l, enum pino p, int rc);

/** Como pino_falha, mas so nas @p n proximas chamadas; depois, sucesso. */
void pino_falha_vezes(enum op o, enum lane_id l, enum pino p, int rc, int n);

/** true se @p spec e o pino @p p da faixa @p l. */
bool pino_e(const struct gpio_dt_spec *spec, enum lane_id l, enum pino p);

/** Zera contagens e historicos do FFF, mantendo niveis, erros e callbacks. */
void fakes_zerar_chamadas(void);

/** Volta tudo ao padrao: sucesso, niveis em 0, nenhum callback. */
void fakes_reset(void);

#endif /* TEST_HW_IO_FAKES_H_ */
