/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file controller.c
 * @brief Laco de servico das duas faixas, sem bloquear.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "controller.h"
#include "hw.h"
#include "hw_adc.h"
#include "hw_uart.h"
#include "st7789.h"

static struct lane lanes[LANE_COUNT];
static uint32_t display_seen; /* ultimo st7789_done() entregue a saida */

static void dispatch(struct lane *lane, enum lane_ev_id id, int64_t now_ms)
{
	struct lane_event ev = { .id = id, .now_ms = now_ms };

	lane_handle_event(lane, &ev);
}

/* Entrega as bordas do feixe na ordem em que aconteceram. */
static void service_beam(struct lane *lane, int64_t now_ms)
{
	uint32_t edges = hw_beam_take_edges(lane->cfg);
	bool rose = (edges & HW_BEAM_ROSE) != 0U;
	bool fell = (edges & HW_BEAM_FELL) != 0U;

	if (rose && fell && (edges & HW_BEAM_LEVEL) != 0U) {
		/* Terminou ativo: saiu e voltou entre dois ticks. */
		dispatch(lane, EV_BEAM_OFF, now_ms);
		dispatch(lane, EV_BEAM_ON, now_ms);
		return;
	}
	if (rose) {
		dispatch(lane, EV_BEAM_ON, now_ms);
	}
	if (fell) {
		dispatch(lane, EV_BEAM_OFF, now_ms);
	}
}

static void service_lane(struct lane *lane, int64_t now_ms)
{
	const struct lane_cfg *cfg = lane->cfg;
	struct lane_event ev = { .now_ms = now_ms };
	int rc;

	rc = hw_limits_read(cfg, &ev.limits.upper, &ev.limits.lower);
	ev.id = (rc < 0) ? EV_HW_ERROR : EV_LIMITS;
	lane_handle_event(lane, &ev);

	service_beam(lane, now_ms);

	ev.id = EV_PLATE;
	while (hw_uart_poll_plate(cfg->camera, &ev.plate)) {
		lane_handle_event(lane, &ev);
	}

	if (cfg->has_payment) {
		uint32_t done = st7789_done();

		/* A falha e lida depois da conclusao: o display publica a falha
		 * de um pedido antes de marca-lo concluido.
		 */
		if (done != display_seen) {
			display_seen = done;
			ev.id = EV_DISPLAY;
			ev.display.done = done;
			ev.display.failed = st7789_failed();
			lane_handle_event(lane, &ev);
		}
		ev.id = EV_PAYMENT;
		while (hw_uart_poll_payment(&ev.payment)) {
			lane_handle_event(lane, &ev);
		}
	}

	dispatch(lane, EV_TICK, now_ms);
}

int controller_init(void)
{
	int rc = hw_io_init();

	if (rc == 0) {
		rc = hw_uart_init();
	}
	if (rc == 0) {
		rc = st7789_start();
	}
	if (rc < 0) {
		return rc;
	}
	/* Uma falha aqui fica travada em hw_adc.c: o primeiro hw_adc_poll()
	 * devolve o erro e as duas faixas vao para LOCKED.
	 */
	(void)hw_adc_init();

	for (size_t i = 0; i < LANE_COUNT; i++) {
		lane_init(&lanes[i], &lane_cfgs[i]);
	}
	display_seen = 0;
	return 0;
}

void controller_step(int64_t now_ms)
{
	struct hw_adc_sample sample;
	int rc = hw_adc_poll(now_ms, &sample);

	if (rc < 0) {
		/* O ADC4 e um so: sem ele nenhuma faixa tem distancia recente. */
		for (size_t i = 0; i < LANE_COUNT; i++) {
			dispatch(&lanes[i], EV_ADC_FAULT, now_ms);
		}
	} else if (rc > 0) {
		struct lane_event ev = {
			.id = EV_DISTANCE,
			.now_ms = now_ms,
			.code = sample.code,
		};

		lane_handle_event(&lanes[sample.lane], &ev);
	}

	for (size_t i = 0; i < LANE_COUNT; i++) {
		service_lane(&lanes[i], now_ms);
	}
}

int controller_run(void)
{
	int64_t next;
	int rc = controller_init();

	if (rc < 0) {
		printk("PARKING_STUDENT_SETUP_FAILED\n");
		return rc;
	}
	printk("PARKING_STUDENT_READY: capacity %d, tariff unit %d ms\n",
	       CONFIG_PARKING_CAPACITY, CONFIG_PARKING_TARIFF_UNIT_MS);

	/* Acorda em instantes absolutos (inicio + n * TICK_MS de tempo
	 * virtual), para o periodo nao escorregar com o tempo de servico.
	 * O primeiro servico espera um tick: a amostra inicial dos sensores.
	 */
	next = k_uptime_get();
	for (;;) {
		next += TICK_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next));
		controller_step(k_uptime_get());
	}
	return 0;
}

const struct lane *controller_lane(enum lane_id id)
{
	return &lanes[id];
}
