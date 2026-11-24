/*
 * Controlador do estacionamento: inicialização e laço principal.
 *
 * Um só laço, a cada TICK_MS, atende as duas faixas. Nada nele pode
 * bloquear. A parada urgente do motor não depende do laço: vem da EXTI,
 * passa por uma work e chega em lane_on_input().
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "adc_sampler.h"
#include "config.h"
#include "display.h"
#include "hw.h"
#include "lane.h"
#include "sessions.h"

static struct lane lanes[LANE_COUNT];

/* Contexto de work: uma entrada da faixa mudou. */
static void on_input(enum lane_id id)
{
	lane_on_input(&lanes[id]);
}

int main(void)
{
	int rc;

	/* As structs ficam prontas antes de qualquer interrupção chegar. */
	for (int i = 0; i < LANE_COUNT; i++) {
		lane_init(&lanes[i], (enum lane_id)i);
	}

	adc_sampler_init();
	sessions_clear();

	rc = hw_init(on_input);
	if (rc != 0) {
		printk("PARKING: falha ao iniciar o hardware (%d)\n", rc);
		return 0;
	}

	for (int i = 0; i < LANE_COUNT; i++) {
		lane_start(&lanes[i], hw_now_ms());
	}

	/*
	 * Display: ~400 ms de esperas do controlador, só agora no boot, com os
	 * motores desligados e o vermelho aceso. Se falhar, a saída pede
	 * assistência ao tentar desenhar uma cobrança.
	 */
	rc = display_init();
	if (rc != 0) {
		printk("PARKING: falha ao iniciar o display (%d)\n", rc);
	}
	printk("PARKING: controlador pronto (capacidade %d)\n", CONFIG_PARKING_CAPACITY);

	for (;;) {
		int64_t now = hw_now_ms();

		adc_sampler_step(now);
		for (int i = 0; i < LANE_COUNT; i++) {
			lane_step(&lanes[i], now);
		}
		k_msleep(TICK_MS);
	}
	return 0;
}
