/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Uma trava de hw_adc_poll por boot: hw_adc.c nao destrava sem reset da
 * maquina, entao esta suite tem um unico teste. O resto do ADC4 esta na
 * suite tests/hw_adc, com os mesmos falsos (shim.h, fakes.c).
 */
#include <errno.h>

#include <zephyr/ztest.h>

#include "fakes.h"
#include "hw_adc.h"

static const struct hw_adc_sample intocada = { .lane = LANE_COUNT, .code = 0xBEEFU };

static void preparar(void *fixture)
{
	ARG_UNUSED(fixture);
	fakes_reset();
	zassert_equal(hw_adc_init(), 0, "init deveria dar certo");
}

ZTEST_SUITE(hw_adc_resultado, NULL, NULL, preparar, NULL, NULL);

ZTEST(hw_adc_resultado, test_resultado_negativo_trava_sem_entregar_amostra)
{
	struct hw_adc_sample s = intocada;

	zassert_equal(hw_adc_poll(0, &s), 0);
	fake_concluir(-EIO, 1000U);
	zassert_equal(hw_adc_poll(10, &s), -EIO, "o erro do driver e devolvido");
	zassert_equal(s.lane, intocada.lane, "nenhuma amostra entregue");
	zassert_equal(s.code, intocada.code);
	zassert_equal(fake_adc_read_async_fake.call_count, 1, "nao ressubmete");

	zassert_equal(hw_adc_poll(20, &s), -EIO);
	zassert_equal(hw_adc_poll(1000, &s), -EIO);
	zassert_equal(fake_adc_read_async_fake.call_count, 1);
}
