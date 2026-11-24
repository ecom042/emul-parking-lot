/*
 * Testes do amostrador do ADC (adc_sampler.c, REQ-10): um pedido por vez,
 * alternância de canais, amostra fresca consumida uma vez, prazo de 100 ms
 * e falhas travadas.
 */
#include <errno.h>

#include <zephyr/ztest.h>

#include "adc_sampler.h"
#include "config.h"
#include "hw_fake.h"

static uint16_t next_raw;

/* Conclusão imediata com o código next_raw. */
static int poll_done(uint16_t *raw)
{
	*raw = next_raw;
	return 0;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	hw_fake_reset();
	adc_sampler_init();
	next_raw = 1000;
	hw_adc_poll_fake.custom_fake = poll_done;
}

ZTEST_SUITE(adc, NULL, NULL, before, NULL, NULL);

ZTEST(adc, test_first_step_starts_entry_channel)
{
	uint16_t code;

	adc_sampler_step(0);
	zassert_equal(hw_adc_start_fake.call_count, 1);
	zassert_equal(hw_adc_start_fake.arg0_val, LANE_ENTRY);
	zassert_equal(hw_adc_poll_fake.call_count, 0, "nada pendente antes");
	zassert_false(adc_sampler_take(LANE_ENTRY, &code), "ainda sem amostra");
}

ZTEST(adc, test_alternates_channels_and_delivers_fresh_once)
{
	uint16_t code = 0;

	adc_sampler_step(0);   /* dispara entrada */
	next_raw = 500;
	adc_sampler_step(10);  /* conclui entrada, dispara saída */
	zassert_equal(hw_adc_start_fake.arg0_history[1], LANE_EXIT);
	zassert_true(adc_sampler_take(LANE_ENTRY, &code));
	zassert_equal(code, 500);
	zassert_false(adc_sampler_take(LANE_ENTRY, &code), "consumida");
	zassert_false(adc_sampler_take(LANE_EXIT, &code));

	next_raw = 3000;
	adc_sampler_step(20);  /* conclui saída, dispara entrada */
	zassert_equal(hw_adc_start_fake.arg0_history[2], LANE_ENTRY);
	zassert_true(adc_sampler_take(LANE_EXIT, &code));
	zassert_equal(code, 3000);
	zassert_false(adc_sampler_failed());
}

ZTEST(adc, test_pending_is_not_resubmitted)
{
	hw_adc_poll_fake.custom_fake = NULL;
	hw_adc_poll_fake.return_val = -EINPROGRESS;
	adc_sampler_step(0);
	for (int t = 10; t < ADC_DEADLINE_MS; t += TICK_MS) {
		adc_sampler_step(t);
	}
	zassert_equal(hw_adc_start_fake.call_count, 1, "um pedido por vez");
	zassert_false(adc_sampler_failed());
}

ZTEST(adc, test_deadline_latches_fault_without_resubmit)
{
	uint16_t code;

	hw_adc_poll_fake.custom_fake = NULL;
	hw_adc_poll_fake.return_val = -EINPROGRESS;
	adc_sampler_step(0);
	adc_sampler_step(ADC_DEADLINE_MS);
	zassert_true(adc_sampler_failed());

	/* Conclusão tardia não recupera nada. */
	hw_adc_poll_fake.custom_fake = poll_done;
	adc_sampler_step(ADC_DEADLINE_MS + 10);
	zassert_true(adc_sampler_failed());
	zassert_equal(hw_adc_start_fake.call_count, 1);
	zassert_false(adc_sampler_take(LANE_ENTRY, &code));
}

ZTEST(adc, test_completion_seen_at_deadline_is_late)
{
	adc_sampler_step(0);
	adc_sampler_step(ADC_DEADLINE_MS); /* já concluída, mas vista tarde */
	zassert_true(adc_sampler_failed());
}

ZTEST(adc, test_submit_error_latches)
{
	hw_adc_start_fake.return_val = -EBUSY;
	adc_sampler_step(0);
	zassert_true(adc_sampler_failed());
	adc_sampler_step(10);
	zassert_equal(hw_adc_start_fake.call_count, 1);
}

ZTEST(adc, test_result_error_latches)
{
	hw_adc_poll_fake.custom_fake = NULL;
	hw_adc_poll_fake.return_val = -EIO;
	adc_sampler_step(0);
	adc_sampler_step(10);
	zassert_true(adc_sampler_failed());
}

ZTEST(adc, test_invalid_sample_latches)
{
	next_raw = ADC_CODE_MAX + 1;
	adc_sampler_step(0);
	adc_sampler_step(10);
	zassert_true(adc_sampler_failed());
}

ZTEST(adc, test_init_clears_fault)
{
	hw_adc_start_fake.return_val = -EIO;
	adc_sampler_step(0);
	zassert_true(adc_sampler_failed());
	adc_sampler_init(); /* reset da máquina */
	zassert_false(adc_sampler_failed());
}
