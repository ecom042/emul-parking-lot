/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Camada de GPIO das faixas (hw_io.c) com os drivers falsos (FFF). O hw_io.c
 * real e compilado com src/shim.h, que troca as chamadas gpio_*_dt pelos
 * falsos; o kernel (k_work, atomic, k_sleep) e o real. As ISRs sao chamadas
 * pelo handler do gpio_callback que hw_io.c entregou a gpio_add_callback_dt,
 * como faria o driver do GPIO na interrupcao.
 */
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "fakes.h"
#include "hw.h"
#include "lane_cfg.h"
#include "parking_limits.h"

static const struct lane_cfg *const entry = &lane_cfgs[LANE_ENTRY];
static const struct lane_cfg *const exit_cfg = &lane_cfgs[LANE_EXIT];

static void antes(void *fixture)
{
	ARG_UNUSED(fixture);
	fakes_reset();
}

/* Deixa terminar qualquer work pendente (buzzer, fim de curso) antes de o
 * proximo teste chamar hw_io_init, que reinicializa os works.
 */
static void depois(void *fixture)
{
	ARG_UNUSED(fixture);
	k_sleep(K_MSEC(BUZZER_MS + 20));
}

ZTEST_SUITE(hw_io, NULL, NULL, antes, depois, NULL);

/* ---- Auxiliares --------------------------------------------------------- */

/** Inicializa com sucesso e zera as contagens dos falsos. */
static void iniciar(void)
{
	zassert_ok(hw_io_init());
	fakes_zerar_chamadas();
}

/** Quantas chamadas de gpio_pin_set_dt foram para o pino dado. */
static int sets_no_pino(enum lane_id l, enum pino p)
{
	unsigned int guardadas =
		MIN(fake_gpio_pin_set_dt_fake.call_count, FFF_ARG_HISTORY_LEN);
	int n = 0;

	for (unsigned int i = 0; i < guardadas; i++) {
		if (pino_e(fake_gpio_pin_set_dt_fake.arg0_history[i], l, p)) {
			n++;
		}
	}
	return n;
}

/** Verifica a i-esima chamada de gpio_pin_set_dt: pino e valor. */
static void set_foi(unsigned int i, enum lane_id l, enum pino p, int valor)
{
	zassert_true(i < MIN(fake_gpio_pin_set_dt_fake.call_count, FFF_ARG_HISTORY_LEN),
		     "set %u nao aconteceu", i);
	zassert_true(pino_e(fake_gpio_pin_set_dt_fake.arg0_history[i], l, p),
		     "set %u no pino errado", i);
	zassert_equal(fake_gpio_pin_set_dt_fake.arg1_history[i], valor,
		      "set %u com valor errado", i);
}

/** Simula a interrupcao de um pino: chama o handler registrado. */
static void interromper(enum lane_id l, enum pino registrado, enum pino causa)
{
	struct gpio_callback *cb = pino_cb[l][registrado];

	zassert_not_null(cb, "callback nao registrado");
	zassert_true(cb->pin_mask & BIT(pinos_ref[l][causa].pin),
		     "pino fora da mascara do callback");
	cb->handler(pinos_ref[l][causa].port, cb, BIT(pinos_ref[l][causa].pin));
}

static void fim_de_curso(enum lane_id l, enum pino p)
{
	interromper(l, P_UPPER, p);
}

static void feixe(enum lane_id l, int nivel)
{
	pino_nivel[l][P_BEAM] = nivel;
	interromper(l, P_BEAM, P_BEAM);
}

/* ---- hw_io_init ----------------------------------------------------------- */

ZTEST(hw_io, test_init_partida_segura_e_ordem)
{
	static const enum pino ordem_cfg[] = {
		P_ENABLE, P_DIRECTION, P_GREEN, P_BUZZER, P_RED,
		P_UPPER, P_LOWER, P_BEAM,
	};

	const fake_gpio_pin_interrupt_configure_dt_Fake *irq_fake =
		&fake_gpio_pin_interrupt_configure_dt_fake;

	zassert_ok(hw_io_init());

	for (int l = 0; l < LANE_COUNT; l++) {
		/* Sequencia de chamadas da faixa: 5 saidas (porto pronto e
		 * configuracao), vermelho, 3 entradas, 2 callbacks, 3
		 * interrupcoes e a leitura do feixe.
		 */
#define CFG fake_gpio_is_ready_dt, fake_gpio_pin_configure_dt
		const void *esperado[] = {
			CFG, CFG, CFG, CFG, CFG, fake_gpio_pin_set_dt,
			CFG, CFG, CFG,
			fake_gpio_add_callback_dt, fake_gpio_add_callback_dt,
			fake_gpio_pin_interrupt_configure_dt,
			fake_gpio_pin_interrupt_configure_dt,
			fake_gpio_pin_interrupt_configure_dt, fake_gpio_pin_get_dt,
		};
#undef CFG

		for (size_t i = 0; i < ARRAY_SIZE(esperado); i++) {
			zassert_equal_ptr(fff.call_history[l * ARRAY_SIZE(esperado) + i],
					  esperado[i], "faixa %d, chamada %u", l,
					  (unsigned int)i);
		}
		for (size_t i = 0; i < ARRAY_SIZE(ordem_cfg); i++) {
			unsigned int k = l * ARRAY_SIZE(ordem_cfg) + i;

			zassert_true(pino_e(fake_gpio_pin_configure_dt_fake.arg0_history[k],
					    l, ordem_cfg[i]),
				     "configuracao %u fora de ordem", k);
			zassert_equal(fake_gpio_pin_configure_dt_fake.arg1_history[k],
				      i < 5 ? GPIO_OUTPUT_INACTIVE : GPIO_INPUT);
		}
		static const enum pino irq_pino[] = { P_UPPER, P_LOWER, P_BEAM };
		static const gpio_flags_t irq_flags[] = {
			GPIO_INT_EDGE_TO_ACTIVE, GPIO_INT_EDGE_TO_ACTIVE,
			GPIO_INT_EDGE_BOTH,
		};

		for (size_t i = 0; i < ARRAY_SIZE(irq_pino); i++) {
			unsigned int k = l * ARRAY_SIZE(irq_pino) + i;

			zassert_true(pino_e(irq_fake->arg0_history[k], l, irq_pino[i]),
				     "interrupcao %u no pino errado", k);
			zassert_equal(irq_fake->arg1_history[k], irq_flags[i],
				      "interrupcao %u com flags erradas", k);
		}

		/* Um callback para os dois fins de curso, outro para o feixe. */
		zassert_equal(pino_cb[l][P_UPPER]->pin_mask,
			      BIT(pinos_ref[l][P_UPPER].pin) | BIT(pinos_ref[l][P_LOWER].pin));
		zassert_equal(pino_cb[l][P_BEAM]->pin_mask, BIT(pinos_ref[l][P_BEAM].pin));

		/* Partida segura: so o vermelho aceso. */
		zassert_equal(pino_nivel[l][P_RED], 1);
		zassert_equal(pino_nivel[l][P_ENABLE], 0);
		zassert_equal(pino_nivel[l][P_GREEN], 0);
		zassert_equal(pino_nivel[l][P_BUZZER], 0);
		zassert_false(hw_motor_running(&lane_cfgs[l]));
		zassert_equal(hw_beam_take_edges(&lane_cfgs[l]), 0);
	}
	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 16);
	zassert_equal(fake_gpio_pin_set_dt_fake.call_count, 2);
}

ZTEST(hw_io, test_init_feixe_ja_ativo_marca_nivel)
{
	pino_nivel[LANE_ENTRY][P_BEAM] = 1;

	zassert_ok(hw_io_init());

	/* Nivel ativo, mas sem borda: a ISR nao viu subida nenhuma. */
	zassert_equal(hw_beam_take_edges(entry), HW_BEAM_LEVEL);
	zassert_equal(hw_beam_take_edges(exit_cfg), 0);
}

ZTEST(hw_io, test_init_porto_nao_pronto)
{
	pino_falha(OP_READY, LANE_ENTRY, P_ENABLE, 1);

	zassert_equal(hw_io_init(), -ENODEV);
	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 0);
	zassert_equal(fake_gpio_pin_set_dt_fake.call_count, 0);
}

ZTEST(hw_io, test_init_porto_de_entrada_nao_pronto)
{
	pino_falha(OP_READY, LANE_ENTRY, P_LOWER, 1);

	zassert_equal(hw_io_init(), -ENODEV);
	/* Saidas e o fim de curso superior ja configurados. */
	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 6);
	zassert_equal(fake_gpio_add_callback_dt_fake.call_count, 0);
}

ZTEST(hw_io, test_init_erro_ao_configurar_saida)
{
	pino_falha(OP_CONFIG, LANE_ENTRY, P_DIRECTION, -EIO);

	zassert_equal(hw_io_init(), -EIO);
	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 2);
	/* Nao acende o vermelho nem segue para as entradas. */
	zassert_equal(fake_gpio_pin_set_dt_fake.call_count, 0);
	zassert_equal(pino_nivel[LANE_ENTRY][P_RED], 0);
}

ZTEST(hw_io, test_init_erro_ao_acender_vermelho)
{
	pino_falha(OP_SET, LANE_ENTRY, P_RED, -EIO);

	zassert_equal(hw_io_init(), -EIO);
	/* So as 5 saidas: as entradas nao sao configuradas. */
	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 5);
	zassert_equal(fake_gpio_add_callback_dt_fake.call_count, 0);
}

ZTEST(hw_io, test_init_erro_ao_configurar_entrada)
{
	pino_falha(OP_CONFIG, LANE_ENTRY, P_BEAM, -EIO);

	zassert_equal(hw_io_init(), -EIO);
	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 8);
	zassert_equal(fake_gpio_add_callback_dt_fake.call_count, 0);
	zassert_equal(fake_gpio_pin_interrupt_configure_dt_fake.call_count, 0);
}

ZTEST(hw_io, test_init_erro_callback_fim_de_curso)
{
	pino_falha(OP_CB, LANE_ENTRY, P_UPPER, -EINVAL);

	zassert_equal(hw_io_init(), -EINVAL);
	zassert_equal(fake_gpio_add_callback_dt_fake.call_count, 1);
	zassert_equal(fake_gpio_pin_interrupt_configure_dt_fake.call_count, 0);
}

ZTEST(hw_io, test_init_erro_callback_feixe)
{
	pino_falha(OP_CB, LANE_ENTRY, P_BEAM, -EINVAL);

	zassert_equal(hw_io_init(), -EINVAL);
	zassert_equal(fake_gpio_add_callback_dt_fake.call_count, 2);
	zassert_equal(fake_gpio_pin_interrupt_configure_dt_fake.call_count, 0);
}

ZTEST(hw_io, test_init_erro_interrupcao_superior)
{
	pino_falha(OP_IRQ, LANE_ENTRY, P_UPPER, -ENOTSUP);

	zassert_equal(hw_io_init(), -ENOTSUP);
	zassert_equal(fake_gpio_pin_interrupt_configure_dt_fake.call_count, 1);
	zassert_equal(fake_gpio_pin_get_dt_fake.call_count, 0);
}

ZTEST(hw_io, test_init_erro_interrupcao_inferior)
{
	pino_falha(OP_IRQ, LANE_ENTRY, P_LOWER, -ENOTSUP);

	zassert_equal(hw_io_init(), -ENOTSUP);
	zassert_equal(fake_gpio_pin_interrupt_configure_dt_fake.call_count, 2);
	zassert_equal(fake_gpio_pin_get_dt_fake.call_count, 0);
}

ZTEST(hw_io, test_init_erro_interrupcao_feixe_nao_le_nivel)
{
	pino_falha(OP_IRQ, LANE_ENTRY, P_BEAM, -ENOTSUP);
	pino_nivel[LANE_ENTRY][P_BEAM] = 1;

	zassert_equal(hw_io_init(), -ENOTSUP);
	zassert_equal(fake_gpio_pin_interrupt_configure_dt_fake.call_count, 3);
	/* Com erro, o feixe nao e lido e o nivel nao e marcado. */
	zassert_equal(fake_gpio_pin_get_dt_fake.call_count, 0);
	zassert_equal(hw_beam_take_edges(entry), 0);
}

ZTEST(hw_io, test_init_erro_na_faixa_de_saida)
{
	pino_falha(OP_IRQ, LANE_EXIT, P_BEAM, -EIO);

	zassert_equal(hw_io_init(), -EIO);
	/* A entrada foi configurada inteira antes da falha da saida. */
	zassert_equal(pino_nivel[LANE_ENTRY][P_RED], 1);
	zassert_equal(pino_nivel[LANE_EXIT][P_RED], 1);
	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 16);
	zassert_equal(fake_gpio_pin_interrupt_configure_dt_fake.call_count, 6);
}

/* ---- Motor -------------------------------------------------------------- */

ZTEST(hw_io, test_motor_abre_direcao_antes_do_enable)
{
	iniciar();
	pino_nivel[LANE_ENTRY][P_BEAM] = 1;

	zassert_ok(hw_motor_drive(entry, true));

	set_foi(0, LANE_ENTRY, P_DIRECTION, 1);
	set_foi(1, LANE_ENTRY, P_ENABLE, 1);
	zassert_equal(fake_gpio_pin_set_dt_fake.call_count, 2);
	zassert_true(hw_motor_running(entry));
	zassert_false(hw_motor_running(exit_cfg));
	/* Abrindo, o feixe ativo nao importa: nem e lido. */
	zassert_equal(fake_gpio_pin_get_dt_fake.call_count, 0);
	zassert_equal(hw_beam_take_edges(entry), 0);
}

ZTEST(hw_io, test_motor_fecha_com_feixe_livre)
{
	iniciar();

	zassert_ok(hw_motor_drive(exit_cfg, false));

	set_foi(0, LANE_EXIT, P_DIRECTION, 0);
	set_foi(1, LANE_EXIT, P_ENABLE, 1);
	zassert_equal(fake_gpio_pin_get_dt_fake.call_count, 1);
	zassert_true(pino_e(fake_gpio_pin_get_dt_fake.arg0_val, LANE_EXIT, P_BEAM));
	zassert_true(hw_motor_running(exit_cfg));
	zassert_equal(pino_nivel[LANE_EXIT][P_ENABLE], 1);
	zassert_equal(hw_beam_take_edges(exit_cfg), 0);
}

ZTEST(hw_io, test_motor_fecha_com_feixe_ativo_corta_e_registra_borda)
{
	iniciar();
	pino_nivel[LANE_ENTRY][P_BEAM] = 1;

	zassert_ok(hw_motor_drive(entry, false));

	set_foi(0, LANE_ENTRY, P_DIRECTION, 0);
	set_foi(1, LANE_ENTRY, P_ENABLE, 1);
	set_foi(2, LANE_ENTRY, P_ENABLE, 0);
	zassert_false(hw_motor_running(entry));
	zassert_equal(pino_nivel[LANE_ENTRY][P_ENABLE], 0);
	zassert_equal(hw_beam_take_edges(entry), HW_BEAM_ROSE | HW_BEAM_LEVEL);
	/* A borda sai na primeira consulta; o nivel fica. */
	zassert_equal(hw_beam_take_edges(entry), HW_BEAM_LEVEL);
}

ZTEST(hw_io, test_motor_fecha_erro_ao_ler_feixe_mantem_motor)
{
	iniciar();
	pino_falha(OP_GET, LANE_ENTRY, P_BEAM, -EIO);

	zassert_ok(hw_motor_drive(entry, false));

	/* Leitura com erro nao conta como feixe ativo. */
	zassert_true(hw_motor_running(entry));
	zassert_equal(pino_nivel[LANE_ENTRY][P_ENABLE], 1);
	zassert_equal(hw_beam_take_edges(entry), 0);
}

ZTEST(hw_io, test_motor_erro_na_direcao_nao_liga)
{
	iniciar();
	pino_falha(OP_SET, LANE_ENTRY, P_DIRECTION, -EIO);

	zassert_equal(hw_motor_drive(entry, false), -EIO);

	zassert_equal(sets_no_pino(LANE_ENTRY, P_ENABLE), 0);
	zassert_false(hw_motor_running(entry));
	/* Nao armou o corte pelo feixe: a ISR nao mexe no enable. */
	feixe(LANE_ENTRY, 1);
	zassert_equal(sets_no_pino(LANE_ENTRY, P_ENABLE), 0);
}

ZTEST(hw_io, test_motor_erro_no_enable_desfaz)
{
	iniciar();
	pino_falha_vezes(OP_SET, LANE_ENTRY, P_ENABLE, -EIO, 1);

	zassert_equal(hw_motor_drive(entry, false), -EIO);

	/* Tentou ligar, falhou, e desligou de novo com sucesso. */
	set_foi(1, LANE_ENTRY, P_ENABLE, 1);
	set_foi(2, LANE_ENTRY, P_ENABLE, 0);
	zassert_false(hw_motor_running(entry));
	/* O corte pelo feixe foi desarmado. */
	fakes_zerar_chamadas();
	feixe(LANE_ENTRY, 1);
	zassert_equal(sets_no_pino(LANE_ENTRY, P_ENABLE), 0);
}

ZTEST(hw_io, test_motor_enable_sem_resposta_continua_comandado)
{
	iniciar();
	pino_falha(OP_SET, LANE_ENTRY, P_ENABLE, -EIO);

	zassert_equal(hw_motor_drive(entry, true), -EIO);

	/* Nem desligar funcionou: o motor continua comandado, para que a
	 * thread de controle tente parar de novo.
	 */
	zassert_true(hw_motor_running(entry));
	zassert_equal(hw_motor_stop(entry), -EIO);
	zassert_true(hw_motor_running(entry));

	pino_falha(OP_SET, LANE_ENTRY, P_ENABLE, 0);
	zassert_ok(hw_motor_stop(entry));
	zassert_false(hw_motor_running(entry));
	zassert_equal(pino_nivel[LANE_ENTRY][P_ENABLE], 0);
}

ZTEST(hw_io, test_motor_stop_desliga_e_desarma_corte)
{
	iniciar();
	zassert_ok(hw_motor_drive(entry, false));
	zassert_true(hw_motor_running(entry));

	zassert_ok(hw_motor_stop(entry));

	zassert_false(hw_motor_running(entry));
	zassert_equal(pino_nivel[LANE_ENTRY][P_ENABLE], 0);
	fakes_zerar_chamadas();
	feixe(LANE_ENTRY, 1);
	zassert_equal(sets_no_pino(LANE_ENTRY, P_ENABLE), 0);
	zassert_equal(hw_beam_take_edges(entry), HW_BEAM_ROSE | HW_BEAM_LEVEL);
}

/* ---- Sinais e buzzer ---------------------------------------------------- */

ZTEST(hw_io, test_luzes_acende_verde_depois_do_vermelho)
{
	iniciar();

	zassert_ok(hw_lights_set(entry, false, true));

	set_foi(0, LANE_ENTRY, P_RED, 0);
	set_foi(1, LANE_ENTRY, P_GREEN, 1);
	zassert_equal(fake_gpio_pin_set_dt_fake.call_count, 2);

	zassert_ok(hw_lights_set(entry, true, true));
	set_foi(2, LANE_ENTRY, P_RED, 1);
	set_foi(3, LANE_ENTRY, P_GREEN, 1);
}

ZTEST(hw_io, test_luzes_apaga_verde_antes_do_vermelho)
{
	iniciar();

	zassert_ok(hw_lights_set(exit_cfg, true, false));

	set_foi(0, LANE_EXIT, P_GREEN, 0);
	set_foi(1, LANE_EXIT, P_RED, 1);

	zassert_ok(hw_lights_set(exit_cfg, false, false));
	set_foi(2, LANE_EXIT, P_GREEN, 0);
	set_foi(3, LANE_EXIT, P_RED, 0);
}

ZTEST(hw_io, test_luzes_erro_no_vermelho_nao_acende_verde)
{
	iniciar();
	pino_falha(OP_SET, LANE_ENTRY, P_RED, -EIO);

	zassert_equal(hw_lights_set(entry, false, true), -EIO);

	zassert_equal(fake_gpio_pin_set_dt_fake.call_count, 1);
	zassert_equal(pino_nivel[LANE_ENTRY][P_GREEN], 0);
}

ZTEST(hw_io, test_luzes_erro_ao_acender_verde)
{
	iniciar();
	pino_falha(OP_SET, LANE_ENTRY, P_GREEN, -EIO);

	zassert_equal(hw_lights_set(entry, false, true), -EIO);

	zassert_equal(fake_gpio_pin_set_dt_fake.call_count, 2);
	zassert_equal(pino_nivel[LANE_ENTRY][P_RED], 0);
	zassert_equal(pino_nivel[LANE_ENTRY][P_GREEN], 0);
}

ZTEST(hw_io, test_luzes_erro_ao_apagar_verde_nao_mexe_no_vermelho)
{
	iniciar();
	pino_falha(OP_SET, LANE_ENTRY, P_GREEN, -EIO);

	zassert_equal(hw_lights_set(entry, false, false), -EIO);

	zassert_equal(fake_gpio_pin_set_dt_fake.call_count, 1);
	zassert_equal(pino_nivel[LANE_ENTRY][P_RED], 1);
}

ZTEST(hw_io, test_luzes_erro_no_vermelho_ao_apagar_verde)
{
	iniciar();
	pino_falha(OP_SET, LANE_ENTRY, P_RED, -EIO);

	zassert_equal(hw_lights_set(entry, false, false), -EIO);

	set_foi(0, LANE_ENTRY, P_GREEN, 0);
	zassert_equal(fake_gpio_pin_set_dt_fake.call_count, 2);
}

ZTEST(hw_io, test_buzzer_pulso_apaga_apos_buzzer_ms)
{
	iniciar();

	zassert_ok(hw_buzzer_pulse(entry));
	zassert_equal(pino_nivel[LANE_ENTRY][P_BUZZER], 1);

	k_sleep(K_MSEC(BUZZER_MS / 2));
	zassert_equal(pino_nivel[LANE_ENTRY][P_BUZZER], 1, "apagou cedo");

	k_sleep(K_MSEC(BUZZER_MS));
	zassert_equal(pino_nivel[LANE_ENTRY][P_BUZZER], 0, "nao apagou");
	set_foi(1, LANE_ENTRY, P_BUZZER, 0);
	zassert_equal(pino_nivel[LANE_EXIT][P_BUZZER], 0);
}

ZTEST(hw_io, test_buzzer_novo_pulso_reinicia_o_prazo)
{
	iniciar();

	zassert_ok(hw_buzzer_pulse(exit_cfg));
	k_sleep(K_MSEC(BUZZER_MS / 2));
	zassert_ok(hw_buzzer_pulse(exit_cfg));

	/* Passou BUZZER_MS do primeiro pulso, mas nao do segundo. */
	k_sleep(K_MSEC(BUZZER_MS * 3 / 4));
	zassert_equal(pino_nivel[LANE_EXIT][P_BUZZER], 1);
	k_sleep(K_MSEC(BUZZER_MS / 2));
	zassert_equal(pino_nivel[LANE_EXIT][P_BUZZER], 0);
	zassert_equal(sets_no_pino(LANE_EXIT, P_BUZZER), 3);
}

ZTEST(hw_io, test_buzzer_erro_nao_agenda_desligamento)
{
	iniciar();
	pino_falha(OP_SET, LANE_ENTRY, P_BUZZER, -EIO);

	zassert_equal(hw_buzzer_pulse(entry), -EIO);

	k_sleep(K_MSEC(BUZZER_MS + 20));
	/* So a tentativa de ligar: nenhum work apagou o buzzer. */
	zassert_equal(fake_gpio_pin_set_dt_fake.call_count, 1);
}

/* ---- Leituras ------------------------------------------------------------ */

ZTEST(hw_io, test_feixe_leitura)
{
	bool ativo = false;

	iniciar();
	pino_nivel[LANE_EXIT][P_BEAM] = 1;
	zassert_ok(hw_beam_read(exit_cfg, &ativo));
	zassert_true(ativo);
	zassert_true(pino_e(fake_gpio_pin_get_dt_fake.arg0_val, LANE_EXIT, P_BEAM));

	pino_nivel[LANE_EXIT][P_BEAM] = 0;
	zassert_ok(hw_beam_read(exit_cfg, &ativo));
	zassert_false(ativo);
}

ZTEST(hw_io, test_feixe_leitura_com_erro_nao_altera_saida)
{
	bool ativo = true;

	iniciar();
	pino_falha(OP_GET, LANE_ENTRY, P_BEAM, -EIO);

	zassert_equal(hw_beam_read(entry, &ativo), -EIO);
	zassert_true(ativo);
}

ZTEST(hw_io, test_fins_de_curso_leitura)
{
	bool upper = false;
	bool lower = true;

	iniciar();
	pino_nivel[LANE_ENTRY][P_UPPER] = 1;
	zassert_ok(hw_limits_read(entry, &upper, &lower));
	zassert_true(upper);
	zassert_false(lower);

	pino_nivel[LANE_ENTRY][P_UPPER] = 0;
	pino_nivel[LANE_ENTRY][P_LOWER] = 1;
	zassert_ok(hw_limits_read(entry, &upper, &lower));
	zassert_false(upper);
	zassert_true(lower);

	/* A saida le os pinos dela. */
	zassert_ok(hw_limits_read(exit_cfg, &upper, &lower));
	zassert_false(upper);
	zassert_false(lower);
}

ZTEST(hw_io, test_fins_de_curso_erro_no_superior)
{
	bool upper = true;
	bool lower = true;

	iniciar();
	pino_falha(OP_GET, LANE_EXIT, P_UPPER, -EIO);

	zassert_equal(hw_limits_read(exit_cfg, &upper, &lower), -EIO);
	zassert_true(upper);
	zassert_true(lower);
}

ZTEST(hw_io, test_fins_de_curso_erro_no_inferior)
{
	bool upper = true;
	bool lower = true;

	iniciar();
	pino_falha(OP_GET, LANE_EXIT, P_LOWER, -EBUSY);

	zassert_equal(hw_limits_read(exit_cfg, &upper, &lower), -EBUSY);
	zassert_true(upper);
	zassert_true(lower);
}

/* ---- Interrupcoes ------------------------------------------------------- */

ZTEST(hw_io, test_isr_fim_de_curso_superior_desliga_no_work)
{
	iniciar();
	zassert_ok(hw_motor_drive(entry, true));
	zassert_ok(hw_motor_drive(exit_cfg, true));

	fim_de_curso(LANE_ENTRY, P_UPPER);
	k_sleep(K_MSEC(5));

	zassert_false(hw_motor_running(entry));
	zassert_equal(pino_nivel[LANE_ENTRY][P_ENABLE], 0);
	/* A outra faixa nao e afetada. */
	zassert_true(hw_motor_running(exit_cfg));
	zassert_equal(pino_nivel[LANE_EXIT][P_ENABLE], 1);
}

ZTEST(hw_io, test_isr_fim_de_curso_inferior_desliga_no_work)
{
	iniciar();
	zassert_ok(hw_motor_drive(exit_cfg, false));

	fim_de_curso(LANE_EXIT, P_LOWER);
	k_sleep(K_MSEC(5));

	zassert_false(hw_motor_running(exit_cfg));
	zassert_equal(pino_nivel[LANE_EXIT][P_ENABLE], 0);
}

ZTEST(hw_io, test_isr_fim_de_curso_com_erro_mantem_comandado)
{
	iniciar();
	zassert_ok(hw_motor_drive(entry, true));
	pino_falha(OP_SET, LANE_ENTRY, P_ENABLE, -EIO);

	fim_de_curso(LANE_ENTRY, P_UPPER);
	k_sleep(K_MSEC(5));

	/* O work tentou desligar; a falha fica visivel para a thread. */
	zassert_equal(sets_no_pino(LANE_ENTRY, P_ENABLE), 2);
	zassert_true(hw_motor_running(entry));
}

ZTEST(hw_io, test_isr_feixe_sobe_fechando_corta_na_hora)
{
	iniciar();
	zassert_ok(hw_motor_drive(entry, false));
	fakes_zerar_chamadas();

	feixe(LANE_ENTRY, 1);

	/* Sem esperar work nenhum: o corte e na propria ISR. */
	set_foi(0, LANE_ENTRY, P_ENABLE, 0);
	zassert_false(hw_motor_running(entry));
	zassert_equal(hw_beam_take_edges(entry), HW_BEAM_ROSE | HW_BEAM_LEVEL);
}

ZTEST(hw_io, test_isr_feixe_sobe_abrindo_nao_corta)
{
	iniciar();
	zassert_ok(hw_motor_drive(entry, true));
	fakes_zerar_chamadas();

	feixe(LANE_ENTRY, 1);

	zassert_equal(fake_gpio_pin_set_dt_fake.call_count, 0);
	zassert_true(hw_motor_running(entry));
	zassert_equal(hw_beam_take_edges(entry), HW_BEAM_ROSE | HW_BEAM_LEVEL);
}

ZTEST(hw_io, test_isr_feixe_sobe_e_desce)
{
	iniciar();

	feixe(LANE_EXIT, 1);
	feixe(LANE_EXIT, 0);

	/* As duas bordas ficam registradas; o nivel e o da ultima. */
	zassert_equal(hw_beam_take_edges(exit_cfg), HW_BEAM_ROSE | HW_BEAM_FELL);
	zassert_equal(hw_beam_take_edges(exit_cfg), 0);
	zassert_equal(hw_beam_take_edges(entry), 0);

	feixe(LANE_EXIT, 0);
	zassert_equal(hw_beam_take_edges(exit_cfg), HW_BEAM_FELL);
}
