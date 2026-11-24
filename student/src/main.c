/**
 * @file main.c
 * @brief Inicialização e laço de atendimento das duas faixas.
 *
 * O laço atende as duas faixas sem esperar conversão do ADC ou desenho do
 * display. A work queue atende as bordas dos fins de curso e dos feixes.
 * O mutex serializa o atendimento do laço e o da work queue.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "display.h"
#include "distance.h"
#include "hw.h"
#include "lane.h"
#include "payment.h"

#define LOOP_PERIOD_MS 1

static K_MUTEX_DEFINE(lock);

/** @brief Borda de entrada, em contexto de work queue: atende a faixa já. */
static void on_input(enum hw_lane lane, enum hw_input input)
{
	ARG_UNUSED(input);

	k_mutex_lock(&lock, K_FOREVER);
	lane_step(lane, k_uptime_get());
	k_mutex_unlock(&lock);
}

int main(void)
{
	int64_t now = k_uptime_get();

	distance_init();
	lane_init(HW_LANE_ENTRY, now);
	lane_init(HW_LANE_EXIT, now);
	/* LCOV_EXCL_START: drivers e módulos não falham no modelo; a falha trava as faixas */
	if (hw_init() < 0 || payment_init() < 0 || display_init() < 0) {
		printk("inicializacao falhou: faixas travadas em assistencia\n");
		lane_fail(HW_LANE_ENTRY, now);
		lane_fail(HW_LANE_EXIT, now);
	}
	/* LCOV_EXCL_STOP */
	hw_set_input_callback(on_input);

	while (true) {
		k_mutex_lock(&lock, K_FOREVER);
		now = k_uptime_get();
		distance_service(now);
		lane_step(HW_LANE_ENTRY, now);
		lane_step(HW_LANE_EXIT, now);
		k_mutex_unlock(&lock);
		k_sleep(K_MSEC(LOOP_PERIOD_MS));
	}
	return 0;
}
