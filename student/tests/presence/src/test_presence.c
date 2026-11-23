/* SPDX-License-Identifier: Apache-2.0 */
/* Presenca: histerese 683/1024 e debounce de 3 amostras consecutivas. */
#include <zephyr/ztest.h>

#include "parking_limits.h"
#include "presence.h"

static struct presence p;

static void limpar(void *fixture)
{
	ARG_UNUSED(fixture);
	p = (struct presence){ 0 };
}

ZTEST_SUITE(presence, NULL, NULL, limpar, NULL, NULL);

/* Entrega n amostras iguais; devolve a ultima transicao. */
static enum presence_change amostras(uint16_t code, int n)
{
	enum presence_change last = PRESENCE_NONE;

	for (int i = 0; i < n; i++) {
		last = presence_update(&p, code);
	}
	return last;
}

ZTEST(presence, test_chegada_so_na_terceira_amostra)
{
	zassert_equal(presence_update(&p, ADC_ARRIVE - 1), PRESENCE_NONE);
	zassert_equal(presence_update(&p, ADC_ARRIVE - 1), PRESENCE_NONE);
	zassert_equal(presence_update(&p, ADC_ARRIVE - 1), PRESENCE_ARRIVED);
	zassert_true(p.present);
}

ZTEST(presence, test_carro_parado_nao_chega_de_novo)
{
	zassert_equal(amostras(500, ADC_DEBOUNCE_N), PRESENCE_ARRIVED);
	zassert_equal(amostras(500, 100), PRESENCE_NONE);
	zassert_true(p.present);
}

ZTEST(presence, test_limiar_exato_nao_chega)
{
	/* 683 nao e < 683. */
	zassert_equal(amostras(ADC_ARRIVE, 10), PRESENCE_NONE);
	zassert_false(p.present);
}

ZTEST(presence, test_ruido_zera_a_contagem)
{
	/* Duas abaixo, uma acima: recomeca do zero. */
	zassert_equal(amostras(ADC_ARRIVE - 1, 2), PRESENCE_NONE);
	zassert_equal(presence_update(&p, ADC_ARRIVE + 10), PRESENCE_NONE);
	zassert_equal(p.count, 0);
	zassert_equal(amostras(ADC_ARRIVE - 1, 2), PRESENCE_NONE);
	zassert_false(p.present);
	zassert_equal(presence_update(&p, ADC_ARRIVE - 1), PRESENCE_ARRIVED);
}

ZTEST(presence, test_histerese_entre_os_limiares)
{
	zassert_equal(amostras(500, ADC_DEBOUNCE_N), PRESENCE_ARRIVED);
	/* Entre 683 e 1024 o carro continua presente. */
	zassert_equal(amostras(900, 10), PRESENCE_NONE);
	zassert_equal(amostras(ADC_LEAVE, 10), PRESENCE_NONE);
	zassert_true(p.present);
}

ZTEST(presence, test_saida_com_debounce)
{
	zassert_equal(amostras(500, ADC_DEBOUNCE_N), PRESENCE_ARRIVED);
	zassert_equal(amostras(ADC_LEAVE + 1, ADC_DEBOUNCE_N - 1), PRESENCE_NONE);
	zassert_equal(presence_update(&p, ADC_LEAVE + 1), PRESENCE_LEFT);
	zassert_false(p.present);
}
