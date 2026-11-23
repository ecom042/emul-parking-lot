/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include <zephyr/ztest.h>

#include "fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(bool, fake_gpio_is_ready_dt, const struct gpio_dt_spec *);
DEFINE_FAKE_VALUE_FUNC(int, fake_gpio_pin_configure_dt, const struct gpio_dt_spec *,
		       gpio_flags_t);
DEFINE_FAKE_VALUE_FUNC(int, fake_gpio_pin_set_dt, const struct gpio_dt_spec *, int);
DEFINE_FAKE_VALUE_FUNC(int, fake_gpio_pin_get_dt, const struct gpio_dt_spec *);
DEFINE_FAKE_VALUE_FUNC(int, fake_gpio_pin_interrupt_configure_dt,
		       const struct gpio_dt_spec *, gpio_flags_t);
DEFINE_FAKE_VALUE_FUNC(int, fake_gpio_add_callback_dt, const struct gpio_dt_spec *,
		       struct gpio_callback *);

#define REF(lane)                                                              \
	{                                                                      \
		[P_ENABLE] = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_motor_enable), gpios), \
		[P_DIRECTION] =                                                \
			GPIO_DT_SPEC_GET(DT_ALIAS(lane##_motor_direction), gpios), \
		[P_RED] = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_light_red), gpios), \
		[P_GREEN] = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_light_green), gpios), \
		[P_BUZZER] = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_buzzer), gpios), \
		[P_UPPER] = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_limit_upper), gpios), \
		[P_LOWER] = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_limit_lower), gpios), \
		[P_BEAM] = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_beam), gpios),     \
	}

const struct gpio_dt_spec pinos_ref[LANE_COUNT][P_COUNT] = {
	[LANE_ENTRY] = REF(entry),
	[LANE_EXIT] = REF(exit),
};

int pino_nivel[LANE_COUNT][P_COUNT];
struct gpio_callback *pino_cb[LANE_COUNT][P_COUNT];
static int erro[OP_COUNT][LANE_COUNT][P_COUNT];
static int vezes[OP_COUNT][LANE_COUNT][P_COUNT]; /* < 0: sempre */

bool pino_e(const struct gpio_dt_spec *spec, enum lane_id l, enum pino p)
{
	return spec->port == pinos_ref[l][p].port && spec->pin == pinos_ref[l][p].pin;
}

/* Acha o pino de um spec; falha o teste se o spec for desconhecido. */
static void achar(const struct gpio_dt_spec *spec, enum lane_id *l, enum pino *p)
{
	*l = LANE_ENTRY;
	*p = P_ENABLE;
	for (int i = 0; i < LANE_COUNT; i++) {
		for (int j = 0; j < P_COUNT; j++) {
			if (pino_e(spec, i, j)) {
				*l = i;
				*p = j;
				return;
			}
		}
	}
	zassert_unreachable("pino desconhecido: porto %p, pino %u", spec->port,
			    spec->pin);
}

void pino_falha_vezes(enum op o, enum lane_id l, enum pino p, int rc, int n)
{
	erro[o][l][p] = rc;
	vezes[o][l][p] = n;
}

void pino_falha(enum op o, enum lane_id l, enum pino p, int rc)
{
	pino_falha_vezes(o, l, p, rc, -1);
}

/* Erro configurado para a operacao no pino de spec (0: sucesso). */
static int consumir(enum op o, const struct gpio_dt_spec *spec)
{
	enum lane_id l;
	enum pino p;
	int rc;

	achar(spec, &l, &p);
	rc = erro[o][l][p];
	if (rc != 0 && vezes[o][l][p] > 0 && --vezes[o][l][p] == 0) {
		erro[o][l][p] = 0;
	}
	return rc;
}

static bool ready_custom(const struct gpio_dt_spec *spec)
{
	return consumir(OP_READY, spec) == 0;
}

static int configure_custom(const struct gpio_dt_spec *spec, gpio_flags_t flags)
{
	ARG_UNUSED(flags);
	return consumir(OP_CONFIG, spec);
}

static int set_custom(const struct gpio_dt_spec *spec, int value)
{
	enum lane_id l;
	enum pino p;
	int rc = consumir(OP_SET, spec);

	if (rc == 0) {
		achar(spec, &l, &p);
		pino_nivel[l][p] = value;
	}
	return rc;
}

static int get_custom(const struct gpio_dt_spec *spec)
{
	enum lane_id l;
	enum pino p;
	int rc = consumir(OP_GET, spec);

	if (rc != 0) {
		return rc;
	}
	achar(spec, &l, &p);
	return pino_nivel[l][p];
}

static int irq_custom(const struct gpio_dt_spec *spec, gpio_flags_t flags)
{
	ARG_UNUSED(flags);
	return consumir(OP_IRQ, spec);
}

static int cb_custom(const struct gpio_dt_spec *spec, struct gpio_callback *cb)
{
	enum lane_id l;
	enum pino p;
	int rc = consumir(OP_CB, spec);

	if (rc == 0) {
		achar(spec, &l, &p);
		pino_cb[l][p] = cb;
	}
	return rc;
}

void fakes_zerar_chamadas(void)
{
	RESET_FAKE(fake_gpio_is_ready_dt);
	RESET_FAKE(fake_gpio_pin_configure_dt);
	RESET_FAKE(fake_gpio_pin_set_dt);
	RESET_FAKE(fake_gpio_pin_get_dt);
	RESET_FAKE(fake_gpio_pin_interrupt_configure_dt);
	RESET_FAKE(fake_gpio_add_callback_dt);
	FFF_RESET_HISTORY();
	fake_gpio_is_ready_dt_fake.custom_fake = ready_custom;
	fake_gpio_pin_configure_dt_fake.custom_fake = configure_custom;
	fake_gpio_pin_set_dt_fake.custom_fake = set_custom;
	fake_gpio_pin_get_dt_fake.custom_fake = get_custom;
	fake_gpio_pin_interrupt_configure_dt_fake.custom_fake = irq_custom;
	fake_gpio_add_callback_dt_fake.custom_fake = cb_custom;
}

void fakes_reset(void)
{
	memset(pino_nivel, 0, sizeof(pino_nivel));
	memset(pino_cb, 0, sizeof(pino_cb));
	memset(erro, 0, sizeof(erro));
	memset(vezes, 0, sizeof(vezes));
	fakes_zerar_chamadas();
}
