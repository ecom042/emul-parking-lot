/* SPDX-License-Identifier: Apache-2.0 */
/* Tarifa inteira: bordas de unidade, minimo e estouro de faixa. */
#include <errno.h>

#include <zephyr/ztest.h>

#include "tariff.h"

ZTEST_SUITE(tariff, NULL, NULL, NULL, NULL, NULL);

ZTEST(tariff, test_unidade_de_60_s)
{
	uint32_t cents = 0;

	zassert_ok(tariff_cents(0, 60000, &cents));
	zassert_equal(cents, 200, "zero ms ainda custa a primeira unidade");

	zassert_ok(tariff_cents(60000, 60000, &cents));
	zassert_equal(cents, 200);

	zassert_ok(tariff_cents(60001, 60000, &cents));
	zassert_equal(cents, 300, "um ms a mais abre a segunda unidade");

	zassert_ok(tariff_cents(120000, 60000, &cents));
	zassert_equal(cents, 300);
}

ZTEST(tariff, test_unidade_de_1_s)
{
	uint32_t cents = 0;

	zassert_ok(tariff_cents(1000, 1000, &cents));
	zassert_equal(cents, 200);

	zassert_ok(tariff_cents(1001, 1000, &cents));
	zassert_equal(cents, 300);

	zassert_ok(tariff_cents(2500, 1000, &cents));
	zassert_equal(cents, 400, "2,5 s sao 3 unidades: BRL 4.00");
}

ZTEST(tariff, test_teto_de_unidades)
{
	uint32_t cents = 0;

	zassert_ok(tariff_cents(9999000, 1000, &cents));
	zassert_equal(cents, 1000000, "9999 unidades atingem o teto do terminal");

	zassert_equal(tariff_cents(10000000, 1000, &cents), -ERANGE,
		      "10000 unidades pedem assistencia");
}

ZTEST(tariff, test_argumentos_invalidos)
{
	uint32_t cents = 0;

	zassert_equal(tariff_cents(1000, 0, &cents), -EINVAL);
	zassert_equal(tariff_cents(-1, 1000, &cents), -EINVAL);
	zassert_equal(tariff_cents(1000, 1000, NULL), -EINVAL);
	zassert_equal(tariff_cents(INT64_MAX, 60000, &cents), -ERANGE,
		      "tempo enorme nao estoura a conta, so a faixa");
}
