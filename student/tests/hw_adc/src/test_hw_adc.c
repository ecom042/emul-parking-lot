/* SPDX-License-Identifier: Apache-2.0 */
/*
 * hw_adc.c com o driver do ADC4 falso (FFF) e o k_poll_signal real.
 *
 * hw_adc.c guarda o estado em variaveis estaticas e nao tem funcao de reset.
 * hw_adc_init() reinicia a faixa e a conversao pendente, mas nao a trava:
 * uma falha trava a aquisicao ate o reset da maquina. Por isso:
 * - a suite "hw_adc" so tem testes que nao travam (before chama hw_adc_init);
 * - a suite "hw_adc_trava" roda depois (o ztest roda as suites em ordem de
 *   nome) e trava por prazo vencido;
 * - hw_adc_init() nao olha a trava, entao as falhas de init ainda podem ser
 *   provocadas depois dela, na suite "hw_adc_trava_init", a ultima;
 * - as outras travas de hw_adc_poll (submissao, resultado, codigo fora da
 *   faixa) precisam de um boot limpo cada: ficam nas suites tests/hw_adc_*.
 */
#include <errno.h>

#include <zephyr/ztest.h>

#include "fakes.h"
#include "hw_adc.h"
#include "parking_limits.h"

#define ADC4_DEV DEVICE_DT_GET(DT_PARENT(DT_ALIAS(entry_distance)))
#define CANAL_ENTRADA 17U
#define CANAL_SAIDA   18U

/* Valor que hw_adc_poll nunca escreve: mostra que "out" nao foi tocado. */
static const struct hw_adc_sample intocada = { .lane = LANE_COUNT, .code = 0xBEEFU };

static void preparar(void *fixture)
{
	ARG_UNUSED(fixture);
	fakes_reset();
	zassert_equal(hw_adc_init(), 0, "init deveria dar certo");
}

ZTEST_SUITE(hw_adc, NULL, NULL, preparar, NULL, NULL);

/* ---- Inicializacao ------------------------------------------------------ */

ZTEST(hw_adc, test_init_configura_os_dois_canais_sem_converter)
{
	/* preparar() ja chamou hw_adc_init() com os falsos zerados. */
	zassert_equal(fake_device_is_ready_fake.call_count, 1);
	zassert_equal_ptr(fake_device_is_ready_fake.arg0_val, ADC4_DEV);
	zassert_equal(fake_adc_channel_setup_fake.call_count, 2);
	zassert_equal_ptr(fake_adc_channel_setup_fake.arg0_history[0], ADC4_DEV);
	zassert_equal(fake_adc_channel_setup_fake.arg1_history[0]->channel_id,
		      CANAL_ENTRADA);
	zassert_equal(fake_adc_channel_setup_fake.arg1_history[1]->channel_id,
		      CANAL_SAIDA);
	zassert_equal(fake_adc_read_async_fake.call_count, 0,
		      "init nao converte nada");
}

/* ---- Submissao ------------------------------------------------------------ */

ZTEST(hw_adc, test_primeira_poll_submete_a_entrada)
{
	struct hw_adc_sample s = intocada;

	zassert_equal(hw_adc_poll(0, &s), 0);
	zassert_equal(fake_adc_read_async_fake.call_count, 1);
	zassert_equal_ptr(fake_adc_read_async_fake.arg0_val, ADC4_DEV);
	zassert_equal(fake_canais[0], BIT(CANAL_ENTRADA));
	zassert_not_null(fake_seq->buffer);
	zassert_equal(fake_seq->buffer_size, sizeof(uint16_t));
	zassert_equal(fake_seq->resolution, 12);
	zassert_not_null(fake_sinal);
	zassert_equal(s.lane, intocada.lane, "sem amostra, out fica intocado");
	zassert_equal(s.code, intocada.code);
}

ZTEST(hw_adc, test_pendente_dentro_do_prazo_nao_ressubmete)
{
	struct hw_adc_sample s = intocada;

	zassert_equal(hw_adc_poll(1000, &s), 0);
	zassert_equal(hw_adc_poll(1010, &s), 0);
	zassert_equal(hw_adc_poll(1000 + ADC_TIMEOUT_MS - 1, &s), 0);
	zassert_equal(fake_adc_read_async_fake.call_count, 1,
		      "uma conversao por vez");
	zassert_equal(s.code, intocada.code);
}

/* ---- Amostras ------------------------------------------------------------- */

ZTEST(hw_adc, test_conclusao_entrega_amostra_e_alterna_as_faixas)
{
	struct hw_adc_sample s = intocada;

	zassert_equal(hw_adc_poll(0, &s), 0);
	fake_concluir(0, 1234U);
	zassert_equal(hw_adc_poll(10, &s), 1);
	zassert_equal(s.lane, LANE_ENTRY);
	zassert_equal(s.code, 1234U);
	zassert_equal(fake_adc_read_async_fake.call_count, 2,
		      "entrega e ja submete a outra faixa");
	zassert_equal(fake_canais[1], BIT(CANAL_SAIDA));

	fake_concluir(0, 321U);
	zassert_equal(hw_adc_poll(20, &s), 1);
	zassert_equal(s.lane, LANE_EXIT);
	zassert_equal(s.code, 321U);
	zassert_equal(fake_canais[2], BIT(CANAL_ENTRADA));
}

ZTEST(hw_adc, test_conclusao_no_ultimo_ms_do_prazo_e_aceita)
{
	struct hw_adc_sample s = intocada;

	zassert_equal(hw_adc_poll(500, &s), 0);
	fake_concluir(0, 4095U);
	zassert_equal(hw_adc_poll(500 + ADC_TIMEOUT_MS - 1, &s), 1);
	zassert_equal(s.lane, LANE_ENTRY);
	zassert_equal(s.code, 4095U, "4095 e o maior codigo de 12 bits");
}

ZTEST(hw_adc, test_prazo_conta_a_partir_da_ultima_submissao)
{
	struct hw_adc_sample s = intocada;

	fake_conv.concluir = true;
	fake_conv.codigo = 7U;
	zassert_equal(hw_adc_poll(0, &s), 0);
	/* Cada amostra ressubmete; o prazo recomeca a cada submissao. */
	for (int64_t t = 50; t <= 500; t += 50) {
		zassert_equal(hw_adc_poll(t, &s), 1, "t=%lld", t);
		zassert_equal(s.code, 7U);
	}
	zassert_equal(fake_adc_read_async_fake.call_count, 11);
}

ZTEST(hw_adc, test_init_reinicia_faixa_e_pendencia)
{
	struct hw_adc_sample s = intocada;

	fake_conv.concluir = true;
	zassert_equal(hw_adc_poll(0, &s), 0);
	zassert_equal(hw_adc_poll(1, &s), 1);
	zassert_equal(s.lane, LANE_ENTRY);
	zassert_equal(fake_canais[1], BIT(CANAL_SAIDA), "pendente: saida");

	zassert_equal(hw_adc_init(), 0);
	fake_conv.concluir = false;
	/* Sem pendencia: a primeira poll submete de novo, pela entrada. */
	zassert_equal(hw_adc_poll(2, &s), 0);
	zassert_equal(fake_adc_read_async_fake.call_count, 3);
	zassert_equal(fake_canais[2], BIT(CANAL_ENTRADA));
}

/* ---- Trava por prazo vencido (por ultimo: nao destrava) ------------------- */

ZTEST_SUITE(hw_adc_trava, NULL, NULL, NULL, NULL, NULL);

ZTEST(hw_adc_trava, test_prazo_vencido_trava_mesmo_com_conclusao_atrasada)
{
	struct hw_adc_sample s = intocada;

	fakes_reset();
	zassert_equal(hw_adc_init(), 0);
	zassert_equal(hw_adc_poll(1000, &s), 0);
	zassert_equal(hw_adc_poll(1000 + ADC_TIMEOUT_MS, &s), -ETIMEDOUT,
		      "a conversao tem de ser vista antes de ADC_TIMEOUT_MS");

	/* A conclusao chega depois do prazo: nao rearma nada. */
	fake_concluir(0, 100U);
	zassert_equal(hw_adc_poll(1200, &s), -ETIMEDOUT);
	zassert_equal(hw_adc_poll(5000, &s), -ETIMEDOUT);
	zassert_equal(fake_adc_read_async_fake.call_count, 1,
		      "travado, nao ressubmete");
	zassert_equal(s.code, intocada.code, "nenhuma amostra entregue");

	/* Nem um novo init destrava: so o reset da maquina. */
	zassert_equal(hw_adc_init(), 0);
	zassert_equal(hw_adc_poll(6000, &s), -ETIMEDOUT);
	zassert_equal(fake_adc_read_async_fake.call_count, 1);
}

/* ---- Falhas do init (depois da trava: o init nao a consulta) -------------- */

ZTEST_SUITE(hw_adc_trava_init, NULL, NULL, NULL, NULL, NULL);

ZTEST(hw_adc_trava_init, test_falhas_do_init_travam_com_o_codigo_do_driver)
{
	static int setup_rc[] = { 0, -EIO };
	struct hw_adc_sample s = intocada;

	/* ADC4 nao pronto: nem configura canal. */
	fakes_reset();
	fake_device_is_ready_fake.return_val = false;
	zassert_equal(hw_adc_init(), -ENODEV);
	zassert_equal(fake_adc_channel_setup_fake.call_count, 0);
	zassert_equal(hw_adc_poll(7000, &s), -ENODEV);

	/* O canal da saida falha: o erro do driver e devolvido e travado. */
	fakes_reset();
	SET_RETURN_SEQ(fake_adc_channel_setup, setup_rc, ARRAY_SIZE(setup_rc));
	zassert_equal(hw_adc_init(), -EIO);
	zassert_equal(fake_adc_channel_setup_fake.call_count, 2);
	zassert_equal(fake_adc_channel_setup_fake.arg1_history[1]->channel_id,
		      CANAL_SAIDA);
	zassert_equal(hw_adc_poll(8000, &s), -EIO);
	zassert_equal(hw_adc_poll(9000, &s), -EIO, "sempre o mesmo erro");
	zassert_equal(fake_adc_read_async_fake.call_count, 0);
	zassert_equal(s.code, intocada.code);
}
