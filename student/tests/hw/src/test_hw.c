/*
 * Testes da camada de hardware real (hw.c) na placa simulada.
 * No início da suíte as cancelas estão fechadas, os feixes livres e os
 * carros a 6000 mm (código 4095).
 */
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "display.h"
#include "hw.h"

static int handler_calls;

static void handler(enum lane_id lane)
{
	ARG_UNUSED(lane);
	handler_calls++;
}

static void *setup(void)
{
	zassert_ok(hw_init(handler));
	return NULL;
}

ZTEST_SUITE(hw, NULL, setup, NULL, NULL, NULL);

ZTEST(hw, test_inputs_at_reset)
{
	for (int lane = 0; lane < LANE_COUNT; lane++) {
		zassert_equal(hw_in_get((enum lane_id)lane, HW_IN_LOWER), 1);
		zassert_equal(hw_in_get((enum lane_id)lane, HW_IN_UPPER), 0);
		zassert_equal(hw_in_get((enum lane_id)lane, HW_IN_BEAM), 0);
	}
}

ZTEST(hw, test_outputs_accept_writes)
{
	for (int lane = 0; lane < LANE_COUNT; lane++) {
		zassert_ok(hw_out_set((enum lane_id)lane, HW_OUT_RED, true));
		zassert_ok(hw_out_set((enum lane_id)lane, HW_OUT_GREEN, false));
	}
}

/* Converte um canal e espera a conclusão sem passar do prazo de 100 ms. */
static int convert(enum lane_id lane, uint16_t *raw)
{
	int64_t start = hw_now_ms();
	int rc;

	zassert_ok(hw_adc_start(lane));
	do {
		rc = hw_adc_poll(raw);
		if (rc != -EINPROGRESS) {
			return rc;
		}
		k_msleep(1);
	} while (hw_now_ms() - start < 100);
	return -ETIMEDOUT;
}

ZTEST(hw, test_adc_reads_far_vehicle_on_both_channels)
{
	uint16_t raw = 0;

	zassert_ok(convert(LANE_ENTRY, &raw));
	zassert_true(raw >= 4095 - 64, "entrada: código %u", raw);
	raw = 0;
	zassert_ok(convert(LANE_EXIT, &raw));
	zassert_true(raw >= 4095 - 64, "saída: código %u", raw);
}

ZTEST(hw, test_adc_poll_is_nonblocking)
{
	uint16_t raw;
	int64_t before = hw_now_ms();
	int rc;

	zassert_ok(hw_adc_start(LANE_ENTRY));
	rc = hw_adc_poll(&raw);
	zassert_true(rc == 0 || rc == -EINPROGRESS, "rc %d", rc);
	zassert_true(hw_now_ms() - before < 10, "poll bloqueou");
	/* Deixa a mesma conversão terminar antes do próximo teste. */
	while (rc == -EINPROGRESS && hw_now_ms() - before < 100) {
		k_msleep(1);
		rc = hw_adc_poll(&raw);
	}
	zassert_ok(rc);
}

ZTEST(hw, test_sched_lock_pairs)
{
	hw_sched_lock();
	hw_sched_unlock();
	zassert_equal(handler_calls, 0, "nenhuma entrada mudou");
}

ZTEST(hw, test_display_draws_charge_screen_quickly)
{
	int64_t start;
	int64_t elapsed;

	zassert_ok(display_init());
	start = hw_now_ms();
	zassert_ok(display_show_plate("ABC1D23"));
	zassert_ok(display_show_amount(400));
	elapsed = hw_now_ms() - start;
	printk("DISPLAY: tela desenhada em %lld ms\n", elapsed);
	/* O laço não pode ficar parado perto do prazo do ADC (100 ms). */
	zassert_true(elapsed < 50, "desenho levou %lld ms", elapsed);
}

ZTEST(hw, test_display_rejects_text_outside_screen)
{
	zassert_equal(display_text(DISPLAY_W - 5, 0, "A"), -EINVAL);
	zassert_equal(display_text(0, DISPLAY_H - 5, "A"), -EINVAL);
}

ZTEST(hw, test_entropy_changes)
{
	uint32_t a = hw_entropy();

	k_busy_wait(10);
	zassert_true(hw_entropy() != a);
}
