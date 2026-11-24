/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Falsos do FFF para os drivers usados por st7789.c (SPI e os pinos D/C e
 * reset) e um registro do que passaria pelo barramento: cada byte escrito com
 * D/C baixo e um comando, com D/C alto e parametro do ultimo comando ou, depois
 * de RAMWR, pixel da janela aberta por CASET/RASET.
 *
 * Os falsos rodam na thread do display: so registram, nunca chamam zassert.
 * Os testes conferem o registro depois de dar tempo a thread.
 */
#ifndef TEST_ST7789_FAKES_H_
#define TEST_ST7789_FAKES_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/fff.h>

#include "st7789.h"

DECLARE_FAKE_VALUE_FUNC(bool, fake_spi_is_ready_dt, const struct spi_dt_spec *);
DECLARE_FAKE_VALUE_FUNC(int, fake_spi_write_dt, const struct spi_dt_spec *,
			const struct spi_buf_set *);
DECLARE_FAKE_VALUE_FUNC(bool, fake_gpio_is_ready_dt, const struct gpio_dt_spec *);
DECLARE_FAKE_VALUE_FUNC(int, fake_gpio_pin_configure_dt, const struct gpio_dt_spec *,
			gpio_flags_t);
DECLARE_FAKE_VALUE_FUNC(int, fake_gpio_pin_set_dt, const struct gpio_dt_spec *, int);

/** Pinos do display, na ordem de pino_ref. */
enum pino_disp {
	PINO_DC,
	PINO_RESET,
	PINO_COUNT,
};

/** Specs de referencia, montados pelo mesmo alias do devicetree. */
extern const struct gpio_dt_spec pino_ref[PINO_COUNT];

#define MAX_CMDS    32
#define MAX_JANELAS 8
#define MAX_RESET   8

/** Um comando e os primeiros parametros dele. */
struct cmd_reg {
	uint8_t cmd;
	uint8_t dados[4];
	uint8_t n; /* total de bytes de parametro recebidos */
};

/** Uma janela aberta por RAMWR e os pixels escritos nela. */
struct janela_reg {
	uint16_t x0;
	uint16_t x1;
	uint16_t y0;
	uint16_t y1;
	uint8_t escala;   /* altura / 7 (0 se a altura nao for multipla de 7) */
	uint32_t bytes;   /* bytes de pixel recebidos */
	uint32_t acesos;  /* pixels 0xffff */
	uint32_t outros;  /* pixels que nao sao 0x0000 nem 0xffff */
	uint32_t excesso; /* pixels alem de largura x altura */
	/* Pixel do canto de cada bloco escala x escala, por coluna: bit r e a
	 * linha r do glifo, como em font.c.
	 */
	uint8_t col[ST7789_WIDTH];
};

/** Tudo o que os falsos viram desde o ultimo fakes_reset(). */
struct barramento {
	struct cmd_reg cmds[MAX_CMDS];
	int n_cmds;
	struct janela_reg jan[MAX_JANELAS];
	int n_jan;
	int escritas;        /* chamadas a spi_write_dt, inclusive as que falharam */
	int dados_soltos;    /* bytes com D/C alto sem comando antes */
	int estouro;         /* comandos ou janelas alem do registro */
	int spec_errado;     /* spi_dt_spec ou pino que nao e do display */
	int reset_nivel[MAX_RESET];
	int n_reset;
	int dc;              /* ultimo nivel escrito em D/C (-1: nenhum) */
};

extern struct barramento bus;

/** Faz a @p n -esima escrita SPI (contando de 1 desde o reset) devolver @p rc. */
void spi_falha_na_escrita(int n, int rc);

/** Valor devolvido por spi_is_ready_dt. */
void spi_pronto(bool pronto);

/** Valor devolvido por gpio_is_ready_dt para o pino @p p. */
void pino_pronto(enum pino_disp p, bool pronto);

/** Valor devolvido por gpio_pin_configure_dt para o pino @p p. */
void pino_erro_config(enum pino_disp p, int rc);

/** true se @p spec e o pino @p p do display. */
bool pino_e(const struct gpio_dt_spec *spec, enum pino_disp p);

/** Volta tudo ao padrao: drivers prontos, sem erros, registro vazio. */
void fakes_reset(void);

#endif /* TEST_ST7789_FAKES_H_ */
