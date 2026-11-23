/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file main.c
 * @brief Inicializacao e a thread de controle unica, a cada TICK_MS.
 *
 * A thread acorda em instantes absolutos (inicio + n * TICK_MS de tempo
 * virtual), para o periodo nao escorregar com o tempo de processamento.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "controller.h"
#include "parking_limits.h"

int main(void)
{
	int64_t next;

	if (controller_init() < 0) {
		printk("PARKING_STUDENT_SETUP_FAILED\n");
		return 0;
	}
	printk("PARKING_STUDENT_READY: capacity %d, tariff unit %d ms\n",
	       CONFIG_PARKING_CAPACITY, CONFIG_PARKING_TARIFF_UNIT_MS);

	/* O primeiro servico espera um tick: a amostra inicial dos sensores. */
	next = k_uptime_get();
	for (;;) {
		next += TICK_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next));
		controller_step(k_uptime_get());
	}
	return 0;
}
