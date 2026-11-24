/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file hw_io.c
 * @brief GPIO e EXTI das duas faixas: motor, sinais, buzzer, feixe e fins de
 *        curso. Unico arquivo que conhece os pinos.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "hw.h"
#include "lane_cfg.h"
#include "parking_limits.h"

/* Bits de io_lane.flags, compartilhados entre ISR, work e thread. */
#define F_MOTOR_ON 0 /* enable comandado alto */
#define F_CLOSING  1 /* fechando: a ISR do feixe corta o motor */
#define F_ROSE     2 /* HW_BEAM_ROSE */
#define F_FELL     3 /* HW_BEAM_FELL */
#define F_LEVEL    4 /* HW_BEAM_LEVEL */

BUILD_ASSERT(HW_BEAM_ROSE == BIT(F_ROSE - F_ROSE), "bordas fora de ordem");
BUILD_ASSERT(HW_BEAM_FELL == BIT(F_FELL - F_ROSE), "bordas fora de ordem");
BUILD_ASSERT(HW_BEAM_LEVEL == BIT(F_LEVEL - F_ROSE), "bordas fora de ordem");

/** Pinos de uma faixa, pelos aliases do devicetree. */
struct io_pins {
	struct gpio_dt_spec enable;
	struct gpio_dt_spec direction;
	struct gpio_dt_spec red;
	struct gpio_dt_spec green;
	struct gpio_dt_spec buzzer;
	struct gpio_dt_spec upper;
	struct gpio_dt_spec lower;
	struct gpio_dt_spec beam;
};

#define IO_PINS(lane)                                                          \
	{                                                                      \
		.enable = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_motor_enable), gpios), \
		.direction =                                                   \
			GPIO_DT_SPEC_GET(DT_ALIAS(lane##_motor_direction), gpios), \
		.red = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_light_red), gpios),    \
		.green = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_light_green), gpios), \
		.buzzer = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_buzzer), gpios),    \
		.upper = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_limit_upper), gpios), \
		.lower = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_limit_lower), gpios), \
		.beam = GPIO_DT_SPEC_GET(DT_ALIAS(lane##_beam), gpios),        \
	}

static const struct io_pins pins[LANE_COUNT] = {
	[LANE_ENTRY] = IO_PINS(entry),
	[LANE_EXIT] = IO_PINS(exit),
};

/** Estado de execucao de uma faixa. */
struct io_lane {
	const struct io_pins *pins;
	atomic_t flags;
	struct gpio_callback limit_cb;
	struct gpio_callback beam_cb;
	struct k_work limit_work;
	struct k_work_delayable buzzer_work;
};

static struct io_lane lanes[LANE_COUNT];

/* Pode rodar em ISR, em work ou na thread: so escreve o enable. */
static int motor_off(struct io_lane *io)
{
	int rc = gpio_pin_set_dt(&io->pins->enable, 0);

	atomic_clear_bit(&io->flags, F_CLOSING);
	if (rc == 0) {
		atomic_clear_bit(&io->flags, F_MOTOR_ON);
	}
	return rc;
}

/* Contexto de work: o fim de curso foi acionado, o motor para aqui. */
static void limit_work_handler(struct k_work *work)
{
	struct io_lane *io = CONTAINER_OF(work, struct io_lane, limit_work);

	/* Uma falha aqui deixa F_MOTOR_ON ligado: a thread de controle ve o
	 * motor ainda comandado e tenta de novo, com o erro tratado por ela.
	 */
	(void)motor_off(io);
}

static void limit_isr(const struct device *port, struct gpio_callback *cb,
		      gpio_port_pins_t pinmask)
{
	struct io_lane *io = CONTAINER_OF(cb, struct io_lane, limit_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pinmask);
	(void)k_work_submit(&io->limit_work);
}

static void beam_isr(const struct device *port, struct gpio_callback *cb,
		     gpio_port_pins_t pinmask)
{
	struct io_lane *io = CONTAINER_OF(cb, struct io_lane, beam_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pinmask);
	if (gpio_pin_get_dt(&io->pins->beam) > 0) {
		/* Obstrucao no fechamento: corta ja, sem esperar a thread. */
		if (atomic_test_bit(&io->flags, F_CLOSING)) {
			(void)motor_off(io);
		}
		atomic_set_bit(&io->flags, F_ROSE);
		atomic_set_bit(&io->flags, F_LEVEL);
	} else {
		atomic_set_bit(&io->flags, F_FELL);
		atomic_clear_bit(&io->flags, F_LEVEL);
	}
}

static void buzzer_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct io_lane *io = CONTAINER_OF(dwork, struct io_lane, buzzer_work);

	(void)gpio_pin_set_dt(&io->pins->buzzer, 0);
}

static struct io_lane *lane_io(const struct lane_cfg *cfg)
{
	return &lanes[cfg->id];
}

/* Configura um grupo de pinos e verifica se os portos estao prontos. */
static int configure(const struct gpio_dt_spec *const specs[], size_t n,
		     gpio_flags_t flags)
{
	for (size_t i = 0; i < n; i++) {
		if (!gpio_is_ready_dt(specs[i])) {
			return -ENODEV;
		}
		int rc = gpio_pin_configure_dt(specs[i], flags);

		if (rc < 0) {
			return rc;
		}
	}
	return 0;
}

static int init_lane(struct io_lane *io, const struct io_pins *p)
{
	const struct gpio_dt_spec *const outputs[] = {
		&p->enable, &p->direction, &p->green, &p->buzzer, &p->red,
	};
	const struct gpio_dt_spec *const inputs[] = {
		&p->upper, &p->lower, &p->beam,
	};
	int rc;

	io->pins = p;
	atomic_clear(&io->flags);
	k_work_init(&io->limit_work, limit_work_handler);
	k_work_init_delayable(&io->buzzer_work, buzzer_work_handler);

	/* Partida segura: tudo inativo, depois o vermelho. */
	rc = configure(outputs, ARRAY_SIZE(outputs), GPIO_OUTPUT_INACTIVE);
	if (rc == 0) {
		rc = gpio_pin_set_dt(&p->red, 1);
	}
	if (rc == 0) {
		rc = configure(inputs, ARRAY_SIZE(inputs), GPIO_INPUT);
	}
	if (rc < 0) {
		return rc;
	}

	/* Os tres sensores da faixa estao no mesmo porto (GPIOC). */
	gpio_init_callback(&io->limit_cb, limit_isr,
			   BIT(p->upper.pin) | BIT(p->lower.pin));
	gpio_init_callback(&io->beam_cb, beam_isr, BIT(p->beam.pin));
	rc = gpio_add_callback_dt(&p->upper, &io->limit_cb);
	if (rc == 0) {
		rc = gpio_add_callback_dt(&p->beam, &io->beam_cb);
	}
	if (rc == 0) {
		rc = gpio_pin_interrupt_configure_dt(&p->upper,
						     GPIO_INT_EDGE_TO_ACTIVE);
	}
	if (rc == 0) {
		rc = gpio_pin_interrupt_configure_dt(&p->lower,
						     GPIO_INT_EDGE_TO_ACTIVE);
	}
	if (rc == 0) {
		rc = gpio_pin_interrupt_configure_dt(&p->beam,
						     GPIO_INT_EDGE_BOTH);
	}
	if (rc == 0 && gpio_pin_get_dt(&p->beam) > 0) {
		atomic_set_bit(&io->flags, F_LEVEL);
	}
	return rc;
}

int hw_io_init(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(lanes); i++) {
		int rc = init_lane(&lanes[i], &pins[i]);

		if (rc < 0) {
			return rc;
		}
	}
	return 0;
}

int hw_motor_drive(const struct lane_cfg *cfg, bool open)
{
	struct io_lane *io = lane_io(cfg);
	int rc = gpio_pin_set_dt(&io->pins->direction, open ? 1 : 0);

	if (rc < 0) {
		return rc;
	}
	if (!open) {
		atomic_set_bit(&io->flags, F_CLOSING);
	}
	atomic_set_bit(&io->flags, F_MOTOR_ON);
	rc = gpio_pin_set_dt(&io->pins->enable, 1);
	if (rc < 0) {
		(void)motor_off(io);
		return rc;
	}
	/* O feixe pode ter ficado ativo antes de a ISR estar armada. */
	if (!open && gpio_pin_get_dt(&io->pins->beam) > 0) {
		(void)motor_off(io);
		atomic_set_bit(&io->flags, F_ROSE);
		atomic_set_bit(&io->flags, F_LEVEL);
	}
	return 0;
}

int hw_motor_stop(const struct lane_cfg *cfg)
{
	return motor_off(lane_io(cfg));
}

bool hw_motor_running(const struct lane_cfg *cfg)
{
	return atomic_test_bit(&lane_io(cfg)->flags, F_MOTOR_ON);
}

int hw_lights_set(const struct lane_cfg *cfg, bool red, bool green)
{
	const struct io_pins *p = lane_io(cfg)->pins;
	int rc;

	if (green) {
		rc = gpio_pin_set_dt(&p->red, red ? 1 : 0);
		if (rc == 0) {
			rc = gpio_pin_set_dt(&p->green, 1);
		}
	} else {
		rc = gpio_pin_set_dt(&p->green, 0);
		if (rc == 0) {
			rc = gpio_pin_set_dt(&p->red, red ? 1 : 0);
		}
	}
	return rc;
}

int hw_buzzer_pulse(const struct lane_cfg *cfg)
{
	struct io_lane *io = lane_io(cfg);
	int rc = gpio_pin_set_dt(&io->pins->buzzer, 1);

	if (rc < 0) {
		return rc;
	}
	(void)k_work_reschedule(&io->buzzer_work, K_MSEC(BUZZER_MS));
	return 0;
}

int hw_beam_read(const struct lane_cfg *cfg, bool *active)
{
	int rc = gpio_pin_get_dt(&lane_io(cfg)->pins->beam);

	if (rc < 0) {
		return rc;
	}
	*active = rc > 0;
	return 0;
}

uint32_t hw_beam_take_edges(const struct lane_cfg *cfg)
{
	struct io_lane *io = lane_io(cfg);
	atomic_val_t old = atomic_and(&io->flags,
				      ~(atomic_val_t)(BIT(F_ROSE) | BIT(F_FELL)));

	return ((uint32_t)old >> F_ROSE) &
	       (HW_BEAM_ROSE | HW_BEAM_FELL | HW_BEAM_LEVEL);
}

int hw_limits_read(const struct lane_cfg *cfg, bool *upper, bool *lower)
{
	const struct io_pins *p = lane_io(cfg)->pins;
	int up = gpio_pin_get_dt(&p->upper);
	int lo = gpio_pin_get_dt(&p->lower);

	if (up < 0) {
		return up;
	}
	if (lo < 0) {
		return lo;
	}
	*upper = up > 0;
	*lower = lo > 0;
	return 0;
}
