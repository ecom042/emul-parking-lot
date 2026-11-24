/* Escalonador do ADC4: uma conversão por vez, alternando, com prazo de 100 ms. */
#include <errno.h>

#include <zephyr/ztest.h>

#include "adc_sampler.h"
#include "fakes.h"

static uint16_t next_raw;
static int next_rc;

static int check_fake(uint16_t *raw)
{
	if (next_rc == 0) {
		*raw = next_raw;
	}
	return next_rc;
}

static void before(void *f)
{
	ARG_UNUSED(f);
	fakes_reset();
	adc_sampler_reset();
	hw_adc_check_fake.custom_fake = check_fake;
	next_raw = 0;
	next_rc = -EINPROGRESS;
}

ZTEST_SUITE(adc_sampler, NULL, NULL, before, NULL, NULL);

ZTEST(adc_sampler, test_alterna_canais)
{
	struct adc_sample s;

	/* 1a volta: dispara a entrada, sem amostra. */
	zassert_false(adc_sampler_poll(0, &s));
	zassert_equal(hw_adc_start_fake.call_count, 1);
	zassert_equal(hw_adc_start_fake.arg0_history[0], LANE_ENTRY);

	/* Pendente: nada novo e nenhum disparo a mais (uma por vez). */
	zassert_false(adc_sampler_poll(2, &s));
	zassert_equal(hw_adc_start_fake.call_count, 1);

	/* Terminou: entrega a amostra da entrada e dispara a saída. */
	next_rc = 0;
	next_raw = 546;
	zassert_true(adc_sampler_poll(4, &s));
	zassert_equal(s.lane, LANE_ENTRY);
	zassert_equal(s.code, 546);
	zassert_equal(hw_adc_start_fake.arg0_history[1], LANE_EXIT);

	next_raw = 4095;
	zassert_true(adc_sampler_poll(6, &s));
	zassert_equal(s.lane, LANE_EXIT);
	zassert_equal(s.code, 4095);
	zassert_false(adc_sampler_failed());
}

ZTEST(adc_sampler, test_prazo_de_100_ms)
{
	struct adc_sample s;

	adc_sampler_poll(1000, &s);
	zassert_false(adc_sampler_poll(1099, &s));
	zassert_false(adc_sampler_failed());
	zassert_false(adc_sampler_poll(1100, &s));
	zassert_true(adc_sampler_failed());
	/* Travada: uma conclusão atrasada não rearma nada. */
	next_rc = 0;
	zassert_false(adc_sampler_poll(1200, &s));
	zassert_equal(hw_adc_start_fake.call_count, 1);
}

ZTEST(adc_sampler, test_erro_do_driver_na_conclusao)
{
	struct adc_sample s;

	adc_sampler_poll(0, &s);
	next_rc = -EIO;
	zassert_false(adc_sampler_poll(2, &s));
	zassert_true(adc_sampler_failed());
}

ZTEST(adc_sampler, test_amostra_impossivel)
{
	struct adc_sample s;

	adc_sampler_poll(0, &s);
	next_rc = 0;
	next_raw = 4096;
	zassert_false(adc_sampler_poll(2, &s));
	zassert_true(adc_sampler_failed());
}

ZTEST(adc_sampler, test_erro_ao_disparar)
{
	struct adc_sample s;

	hw_adc_start_fake.return_val = -EBUSY;
	zassert_false(adc_sampler_poll(0, &s));
	zassert_true(adc_sampler_failed());
}

ZTEST(adc_sampler, test_erro_ao_disparar_depois_de_amostra)
{
	struct adc_sample s;

	adc_sampler_poll(0, &s);
	next_rc = 0;
	next_raw = 100;
	hw_adc_start_fake.return_val = -EIO;
	/* A amostra que terminou ainda é entregue; a falha vale dali em diante. */
	zassert_true(adc_sampler_poll(2, &s));
	zassert_equal(s.code, 100);
	zassert_true(adc_sampler_failed());
}
