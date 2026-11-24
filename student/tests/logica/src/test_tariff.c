/* Tarifa: 200 + (unidades - 1) * 100, unidades = max(1, ceil(t / u)). */
#include <errno.h>

#include <zephyr/ztest.h>

#include "tariff.h"

ZTEST_SUITE(tariff, NULL, NULL, NULL, NULL, NULL);

ZTEST(tariff, test_exemplo_do_enunciado)
{
	uint32_t v = 0;

	/* 2,5 s com unidade de 1 s = 3 unidades = 400 centavos (BRL 4.00). */
	zassert_ok(tariff_amount(2500, 1000, &v));
	zassert_equal(v, 400);
}

ZTEST(tariff, test_minimo_uma_unidade)
{
	uint32_t v = 0;

	zassert_ok(tariff_amount(0, 60000, &v));
	zassert_equal(v, 200);
}

ZTEST(tariff, test_limite_da_unidade)
{
	uint32_t v = 0;

	/* Exemplo da especificação: 60000 ms custa 200, 60001 ms custa 300. */
	zassert_ok(tariff_amount(60000, 60000, &v));
	zassert_equal(v, 200);
	zassert_ok(tariff_amount(60001, 60000, &v));
	zassert_equal(v, 300);
}

ZTEST(tariff, test_maior_valor_e_estouro)
{
	uint32_t v = 0;

	zassert_ok(tariff_amount(9999LL * 1000, 1000, &v));
	zassert_equal(v, 1000000);
	v = 7;
	zassert_equal(tariff_amount(9999LL * 1000 + 1, 1000, &v), -ERANGE);
	zassert_equal(v, 7, "valor não pode mudar no erro");
}

ZTEST(tariff, test_argumentos_invalidos)
{
	uint32_t v = 0;

	zassert_equal(tariff_amount(1000, 0, &v), -EINVAL);
	zassert_equal(tariff_amount(-1, 1000, &v), -EINVAL);
}
