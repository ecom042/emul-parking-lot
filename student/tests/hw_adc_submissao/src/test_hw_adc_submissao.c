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

#define CANAL_SAIDA 18U

static const struct hw_adc_sample intocada = { .lane = LANE_COUNT, .code = 0xBEEFU };

static void preparar(void *fixture)
{
	ARG_UNUSED(fixture);
	fakes_reset();
	zassert_equal(hw_adc_init(), 0, "init deveria dar certo");
}

ZTEST_SUITE(hw_adc_submissao, NULL, NULL, preparar, NULL, NULL);

ZTEST(hw_adc_submissao, test_falha_ao_submeter_trava_e_vence_a_amostra)
{
	struct hw_adc_sample s = intocada;

	fake_conv.concluir = true;
	fake_conv.codigo = 900U;
	zassert_equal(hw_adc_poll(0, &s), 0);

	/* A amostra da entrada chega, mas a submissao da saida falha. */
	fake_conv.rc = -EBUSY;
	zassert_equal(hw_adc_poll(10, &s), -EBUSY,
		      "a falha de submissao vale mais que a amostra");
	zassert_equal(fake_adc_read_async_fake.call_count, 2);
	zassert_equal(fake_canais[1], BIT(CANAL_SAIDA));

	/* Travado: o driver volta a aceitar, mas nada e submetido. */
	fake_conv.rc = 0;
	zassert_equal(hw_adc_poll(20, &s), -EBUSY);
	zassert_equal(hw_adc_poll(1000, &s), -EBUSY);
	zassert_equal(fake_adc_read_async_fake.call_count, 2);
}
