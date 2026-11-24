/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(bool, fake_spi_is_ready_dt, const struct spi_dt_spec *);
DEFINE_FAKE_VALUE_FUNC(int, fake_spi_write_dt, const struct spi_dt_spec *,
		       const struct spi_buf_set *);
DEFINE_FAKE_VALUE_FUNC(bool, fake_gpio_is_ready_dt, const struct gpio_dt_spec *);
DEFINE_FAKE_VALUE_FUNC(int, fake_gpio_pin_configure_dt, const struct gpio_dt_spec *,
		       gpio_flags_t);
DEFINE_FAKE_VALUE_FUNC(int, fake_gpio_pin_set_dt, const struct gpio_dt_spec *, int);

#define DISPLAY DT_ALIAS(payment_display)

#define CMD_CASET 0x2a
#define CMD_RASET 0x2b
#define CMD_RAMWR 0x2c

const struct gpio_dt_spec pino_ref[PINO_COUNT] = {
	[PINO_DC] = GPIO_DT_SPEC_GET(DISPLAY, dc_gpios),
	[PINO_RESET] = GPIO_DT_SPEC_GET(DISPLAY, reset_gpios),
};

static const struct spi_dt_spec spi_ref =
	SPI_DT_SPEC_GET(DISPLAY, SPI_OP_MODE_CONTROLLER | SPI_WORD_SET(8));

struct barramento bus;

static bool spi_ok;
static int falha_n;
static int falha_rc;
static bool pronto[PINO_COUNT];
static int erro_config[PINO_COUNT];

/* Estado do decodificador: ultimo comando e o pixel corrente da janela. */
static int ultimo_cmd;
static int caset = -1;
static int raset = -1;
static uint32_t pixel;
static uint8_t meio_pixel;
static bool tem_meio;

void spi_falha_na_escrita(int n, int rc)
{
	falha_n = n;
	falha_rc = rc;
}

void spi_pronto(bool p)
{
	spi_ok = p;
}

void pino_pronto(enum pino_disp p, bool sim)
{
	pronto[p] = sim;
}

void pino_erro_config(enum pino_disp p, int rc)
{
	erro_config[p] = rc;
}

bool pino_e(const struct gpio_dt_spec *spec, enum pino_disp p)
{
	return spec->port == pino_ref[p].port && spec->pin == pino_ref[p].pin;
}

/* Indice do pino do display, ou PINO_COUNT (e conta como spec errado). */
static enum pino_disp achar(const struct gpio_dt_spec *spec)
{
	for (int p = 0; p < PINO_COUNT; p++) {
		if (pino_e(spec, p)) {
			return p;
		}
	}
	bus.spec_errado++;
	return PINO_COUNT;
}

static uint16_t coord(const struct cmd_reg *c, int alto, int baixo)
{
	return (uint16_t)((c->dados[alto] << 8) | c->dados[baixo]);
}

static void abre_janela(void)
{
	struct janela_reg *j;
	uint16_t h;

	if (bus.n_jan >= MAX_JANELAS || caset < 0 || raset < 0) {
		bus.estouro++;
		return;
	}
	j = &bus.jan[bus.n_jan++];
	memset(j, 0, sizeof(*j));
	j->x0 = coord(&bus.cmds[caset], 0, 1);
	j->x1 = coord(&bus.cmds[caset], 2, 3);
	j->y0 = coord(&bus.cmds[raset], 0, 1);
	j->y1 = coord(&bus.cmds[raset], 2, 3);
	h = (uint16_t)(j->y1 - j->y0 + 1);
	j->escala = (h % 7 == 0) ? (uint8_t)(h / 7) : 0;
	pixel = 0;
	tem_meio = false;
}

static void recebe_pixel(struct janela_reg *j, uint16_t cor)
{
	uint32_t w = (uint32_t)(j->x1 - j->x0 + 1);
	uint32_t h = (uint32_t)(j->y1 - j->y0 + 1);
	uint32_t x = pixel % w;
	uint32_t y = pixel / w;

	pixel++;
	if (y >= h) {
		j->excesso++;
		return;
	}
	if (cor == 0x0000) {
		return;
	}
	if (cor != 0xffff) {
		j->outros++;
		return;
	}
	j->acesos++;
	if (j->escala != 0 && x % j->escala == 0 && y % j->escala == 0 &&
	    x / j->escala < ST7789_WIDTH) {
		j->col[x / j->escala] |= (uint8_t)(1U << (y / j->escala));
	}
}

static void recebe_dado(uint8_t b)
{
	struct cmd_reg *c;
	struct janela_reg *j;

	if (bus.n_cmds == 0) {
		bus.dados_soltos++;
		return;
	}
	if (ultimo_cmd == CMD_RAMWR) {
		if (bus.n_jan == 0) {
			return;
		}
		j = &bus.jan[bus.n_jan - 1];
		j->bytes++;
		if (tem_meio) {
			recebe_pixel(j, (uint16_t)((meio_pixel << 8) | b));
		} else {
			meio_pixel = b;
		}
		tem_meio = !tem_meio;
		return;
	}
	c = &bus.cmds[bus.n_cmds - 1];
	if (c->n < sizeof(c->dados)) {
		c->dados[c->n] = b;
	}
	c->n++;
}

static void recebe_cmd(uint8_t b)
{
	if (bus.n_cmds >= MAX_CMDS) {
		bus.estouro++;
		return;
	}
	memset(&bus.cmds[bus.n_cmds], 0, sizeof(bus.cmds[0]));
	bus.cmds[bus.n_cmds].cmd = b;
	if (b == CMD_CASET) {
		caset = bus.n_cmds;
	} else if (b == CMD_RASET) {
		raset = bus.n_cmds;
	}
	bus.n_cmds++;
	ultimo_cmd = b;
	if (b == CMD_RAMWR) {
		abre_janela();
	}
}

static int write_custom(const struct spi_dt_spec *spec, const struct spi_buf_set *set)
{
	bus.escritas++;
	if (spec->bus != spi_ref.bus || spec->config.slave != spi_ref.config.slave ||
	    spec->config.operation != spi_ref.config.operation) {
		bus.spec_errado++;
	}
	if (falha_n != 0 && bus.escritas == falha_n) {
		return falha_rc;
	}
	for (size_t i = 0; i < set->count; i++) {
		const uint8_t *p = set->buffers[i].buf;

		for (size_t k = 0; k < set->buffers[i].len; k++) {
			if (bus.dc == 0) {
				recebe_cmd(p[k]);
			} else {
				recebe_dado(p[k]);
			}
		}
	}
	return 0;
}

static bool spi_ready_custom(const struct spi_dt_spec *spec)
{
	if (spec->bus != spi_ref.bus) {
		bus.spec_errado++;
	}
	return spi_ok;
}

static bool gpio_ready_custom(const struct gpio_dt_spec *spec)
{
	enum pino_disp p = achar(spec);

	return p < PINO_COUNT && pronto[p];
}

static int configure_custom(const struct gpio_dt_spec *spec, gpio_flags_t flags)
{
	enum pino_disp p = achar(spec);

	ARG_UNUSED(flags);
	return p < PINO_COUNT ? erro_config[p] : -EINVAL;
}

static int set_custom(const struct gpio_dt_spec *spec, int value)
{
	enum pino_disp p = achar(spec);

	if (p == PINO_DC) {
		bus.dc = value;
	} else if (p == PINO_RESET) {
		if (bus.n_reset < MAX_RESET) {
			bus.reset_nivel[bus.n_reset] = value;
		}
		bus.n_reset++;
	}
	return 0;
}

void fakes_reset(void)
{
	RESET_FAKE(fake_spi_is_ready_dt);
	RESET_FAKE(fake_spi_write_dt);
	RESET_FAKE(fake_gpio_is_ready_dt);
	RESET_FAKE(fake_gpio_pin_configure_dt);
	RESET_FAKE(fake_gpio_pin_set_dt);
	FFF_RESET_HISTORY();
	fake_spi_is_ready_dt_fake.custom_fake = spi_ready_custom;
	fake_spi_write_dt_fake.custom_fake = write_custom;
	fake_gpio_is_ready_dt_fake.custom_fake = gpio_ready_custom;
	fake_gpio_pin_configure_dt_fake.custom_fake = configure_custom;
	fake_gpio_pin_set_dt_fake.custom_fake = set_custom;

	memset(&bus, 0, sizeof(bus));
	bus.dc = -1;
	spi_ok = true;
	falha_n = 0;
	falha_rc = 0;
	for (int p = 0; p < PINO_COUNT; p++) {
		pronto[p] = true;
		erro_config[p] = 0;
	}
	ultimo_cmd = -1;
	caset = -1;
	raset = -1;
	pixel = 0;
	tem_meio = false;
}
