/*
 * test_tariff.c — tarifa (REQ-12): 200 + (unidades - 1) * 100, com
 * unidades = max(1, ceil(tempo / unidade)), e assistência acima de 9999 unidades.
 * Função pura: nenhum falso necessário.
 */
#include <zephyr/ztest.h>

#include "tariff.h"

ZTEST_SUITE(tariff, NULL, NULL, NULL, NULL, NULL);

static uint32_t amount_for(int64_t elapsed, uint32_t unit)
{
	uint32_t amount = 0U;

	zassert_true(tariff_compute(1000, 1000 + elapsed, unit, &amount));
	return amount;
}

ZTEST(tariff, test_example_from_the_challenge)
{
	/* 2,5 s com unidade de 1000 ms: 3 unidades = 400 centavos (BRL 4.00) */
	zassert_equal(amount_for(2500, 1000), 400U);
}

ZTEST(tariff, test_minimum_is_one_unit)
{
	zassert_equal(amount_for(0, 1000), 200U);
	zassert_equal(amount_for(1, 1000), 200U);
	zassert_equal(amount_for(1000, 1000), 200U); /* exatamente 1 unidade */
}

ZTEST(tariff, test_rounds_up_to_the_next_unit)
{
	zassert_equal(amount_for(1001, 1000), 300U);
	zassert_equal(amount_for(2000, 1000), 300U);
	zassert_equal(amount_for(2001, 1000), 400U);
	zassert_equal(amount_for(120001, 60000), 400U); /* unidade padrão do Kconfig */
}

ZTEST(tariff, test_limit_9999_units_is_exactly_the_terminal_maximum)
{
	uint32_t amount = 0U;

	zassert_equal(amount_for(9999 * 1000, 1000), 1000000U);
	zassert_false(tariff_compute(0, 9999 * 1000 + 1, 1000, &amount));
	zassert_equal(amount, 0U); /* não é alterado em caso de erro */
}

ZTEST(tariff, test_invalid_inputs)
{
	uint32_t amount = 0U;

	zassert_false(tariff_compute(0, 100, 0, &amount)); /* unidade zero */
	zassert_true(tariff_compute(500, 100, 1000, &amount)); /* relógio voltou: 1 unidade */
	zassert_equal(amount, 200U);
	zassert_false(tariff_compute(0, INT64_MAX, 1000, &amount)); /* sem estouro de conta */
}
