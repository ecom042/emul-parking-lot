/**
 * @file test_tariff.c
 * @brief Testes da tarifa (tariff.c): limites das unidades e argumentos.
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/ztest.h>

#include "tariff.h"

ZTEST_SUITE(tariff, NULL, NULL, NULL, NULL, NULL);

/** @brief Calcula a tarifa e confere que deu certo. */
static uint32_t price(int64_t elapsed_ms, uint32_t unit_ms)
{
	uint32_t amount = 0;

	zassert_ok(tariff_compute(elapsed_ms, unit_ms, &amount));
	return amount;
}

ZTEST(tariff, test_unit_1000_boundaries)
{
	zassert_equal(price(0, 1000), 200);
	zassert_equal(price(1, 1000), 200);
	zassert_equal(price(999, 1000), 200);
	zassert_equal(price(1000, 1000), 200);
	zassert_equal(price(1001, 1000), 300);
	zassert_equal(price(2000, 1000), 300);
	zassert_equal(price(2500, 1000), 400);
}

ZTEST(tariff, test_unit_60000_boundaries)
{
	zassert_equal(price(60000, 60000), 200);
	zassert_equal(price(60001, 60000), 300);
	zassert_equal(price(120000, 60000), 300);
	zassert_equal(price(120001, 60000), 400);
}

ZTEST(tariff, test_max_units_passes)
{
	zassert_equal(price((int64_t)TARIFF_MAX_UNITS * 1000, 1000), 1000000);
	zassert_equal(price((int64_t)TARIFF_MAX_UNITS * 60000, 60000), 1000000);
}

ZTEST(tariff, test_above_max_units_is_erange)
{
	uint32_t amount = 1234;

	zassert_equal(tariff_compute((int64_t)TARIFF_MAX_UNITS * 1000 + 1, 1000, &amount),
		      -ERANGE);
	zassert_equal(tariff_compute((int64_t)(TARIFF_MAX_UNITS + 1) * 1000, 1000, &amount),
		      -ERANGE);
	zassert_equal(tariff_compute(INT64_MAX, 1000, &amount), -ERANGE);
	zassert_equal(amount, 1234, "valor não pode mudar no erro");
}

ZTEST(tariff, test_invalid_arguments)
{
	uint32_t amount = 1234;

	zassert_equal(tariff_compute(-1, 1000, &amount), -EINVAL);
	zassert_equal(tariff_compute(INT64_MIN, 1000, &amount), -EINVAL);
	zassert_equal(tariff_compute(1000, 0, &amount), -EINVAL);
	zassert_equal(tariff_compute(1000, 1000, NULL), -EINVAL);
	zassert_equal(amount, 1234, "valor não pode mudar no erro");
}
