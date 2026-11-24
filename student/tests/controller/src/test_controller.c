/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Thread de controle (controller.c) com a logica real das faixas e a camada
 * de hardware falsa (FFF). Cada teste monta o "mundo" (fins de curso, bordas
 * do feixe, ADC4, quadros das UARTs), chama controller_step() com tempo
 * virtual explicito e confere o efeito pelo estado das faixas
 * (controller_lane()) e pelas chamadas e argumentos dos falsos.
 */
#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "controller.h"
#include "fakes.h"
#include "sessions.h"

#define PERTO 500U  /* codigo de carro a ~730 mm: chegada */
#define LONGE 2000U /* codigo de carro a ~2900 mm: foi embora */
#define PLACA "ABC1D23"

static const struct lane_cfg *const entry = &lane_cfgs[LANE_ENTRY];
static const struct lane_cfg *const exit_cfg = &lane_cfgs[LANE_EXIT];

/* Tempo virtual do proximo tick. */
static int64_t agora;

static const struct lane *faixa(enum lane_id id)
{
	return controller_lane(id);
}

/* Inicializa com sucesso e zera o historico dos falsos. */
static void preparar(void *fixture)
{
	ARG_UNUSED(fixture);
	mundo_reset();
	fakes_reset();
	sessions_reset();
	zassert_equal(controller_init(), 0);
	fakes_reset();
	agora = 0;
}

ZTEST_SUITE(controller, NULL, NULL, preparar, NULL, NULL);

/* ---- Roteiros ---------------------------------------------------------- */

/** Um tick de servico, TICK_MS depois do anterior. */
static void passo(void)
{
	agora += TICK_MS;
	controller_step(agora);
}

/** Um tick em que o ADC4 entrega @p code para a faixa @p id. */
static void passo_com_amostra(enum lane_id id, uint16_t code)
{
	mundo.adc_rc = 1;
	mundo.adc.lane = id;
	mundo.adc.code = code;
	passo();
	mundo.adc_rc = 0;
}

/** Carro chega na faixa @p id: ADC_DEBOUNCE_N amostras perto. */
static void chegar(enum lane_id id)
{
	for (int i = 0; i < ADC_DEBOUNCE_N; i++) {
		passo_com_amostra(id, PERTO);
	}
	zassert_equal(faixa(id)->state, LANE_IDENTIFYING);
	zassert_not_equal(faixa(id)->capture_id, 0U);
}

/** Entrada: partida fechada, carro chega, placa boa, cancela abre. */
static void entrada_aberta(void)
{
	passo();
	chegar(LANE_ENTRY);
	mundo_placa(HW_UART_ENTRY_CAMERA, faixa(LANE_ENTRY)->capture_id, PLACA,
		    93);
	passo();
	zassert_equal(faixa(LANE_ENTRY)->state, LANE_OPENING);

	mundo.upper[LANE_ENTRY] = true;
	mundo.lower[LANE_ENTRY] = false;
	passo();
	zassert_equal(faixa(LANE_ENTRY)->state, LANE_WAIT_PASSAGE);
	zassert_false(faixa(LANE_ENTRY)->beam_seen);
}

/** Saida: sessao ativa, carro chega, placa boa: a placa vai para a tela. */
static void saida_mostrando_placa(void)
{
	zassert_equal(sessions_add(PLACA, 0), 0);
	passo();
	chegar(LANE_EXIT);
	mundo_placa(HW_UART_EXIT_CAMERA, faixa(LANE_EXIT)->capture_id, PLACA,
		    93);
	passo();
	zassert_equal(faixa(LANE_EXIT)->state, LANE_DISPLAYING);
	zassert_equal(st7789_show_plate_fake.call_count, 1);
	zassert_str_equal(st7789_show_plate_fake.arg0_val, PLACA);
	zassert_equal(faixa(LANE_EXIT)->disp_ticket, mundo.disp_emitidos);
	zassert_equal(st7789_show_charge_fake.call_count, 0, "so depois da placa");
}

/**
 * Saida ate cobrando: o display conclui a placa (preco congelado, valor
 * pedido) e depois o valor (UUID e P1 CHARGE).
 */
static void saida_cobrando(void)
{
	saida_mostrando_placa();

	mundo_display_conclui();
	passo();
	zassert_equal(faixa(LANE_EXIT)->state, LANE_DISPLAYING);
	zassert_true(faixa(LANE_EXIT)->price_frozen);
	zassert_equal(st7789_show_charge_fake.call_count, 1);
	zassert_equal(st7789_show_charge_fake.arg0_val, faixa(LANE_EXIT)->cents);
	zassert_equal(faixa(LANE_EXIT)->uuid[0], '\0', "ainda sem cobranca");

	mundo_display_conclui();
	passo();
	zassert_equal(faixa(LANE_EXIT)->state, LANE_CHARGING);
	zassert_not_equal(faixa(LANE_EXIT)->uuid[0], '\0');
}

/** Quantas linhas foram enviadas ao terminal de pagamento. */
static int envios_ao_terminal(void)
{
	int n = 0;

	for (unsigned int i = 0; i < hw_uart_send_fake.call_count &&
				 i < FFF_ARG_HISTORY_LEN; i++) {
		if (hw_uart_send_fake.arg0_history[i] == HW_UART_PAYMENT) {
			n++;
		}
	}
	return n;
}

/** Posicao da primeira chamada a @p fn no historico, ou -1. */
static int posicao(void *fn)
{
	for (int i = 0; i < (int)fff.call_history_idx; i++) {
		if (fff.call_history[i] == fn) {
			return i;
		}
	}
	return -1;
}

/* ---- controller_init ---------------------------------------------------- */

ZTEST(controller, test_init_liga_o_hardware_em_ordem)
{
	fakes_reset();
	zassert_equal(controller_init(), 0);

	zassert_equal(fff.call_history[0], (void *)hw_io_init);
	zassert_equal(fff.call_history[1], (void *)hw_uart_init);
	zassert_equal(fff.call_history[2], (void *)st7789_start);
	zassert_equal(fff.call_history[3], (void *)hw_adc_init);

	/* Partida segura das duas faixas, cada uma com a sua configuracao. */
	zassert_equal(hw_motor_stop_fake.call_count, 2);
	zassert_equal(hw_motor_stop_fake.arg0_history[0], entry);
	zassert_equal(hw_motor_stop_fake.arg0_history[1], exit_cfg);
	for (int i = 0; i < LANE_COUNT; i++) {
		zassert_equal(faixa(i)->cfg, &lane_cfgs[i]);
		zassert_equal(faixa(i)->state, LANE_IDLE);
		zassert_false(faixa(i)->started, "so o 1o EV_LIMITS classifica");
	}
}

ZTEST(controller, test_init_erro_no_gpio_para_tudo)
{
	fakes_reset();
	hw_io_init_fake.return_val = -EIO;

	zassert_equal(controller_init(), -EIO);
	zassert_equal(hw_uart_init_fake.call_count, 0);
	zassert_equal(st7789_start_fake.call_count, 0);
	zassert_equal(hw_adc_init_fake.call_count, 0);
	zassert_equal(hw_motor_stop_fake.call_count, 0, "faixas nao iniciadas");
}

ZTEST(controller, test_init_erro_na_uart_para_antes_do_display)
{
	fakes_reset();
	hw_uart_init_fake.return_val = -ENODEV;

	zassert_equal(controller_init(), -ENODEV);
	zassert_equal(hw_io_init_fake.call_count, 1);
	zassert_equal(st7789_start_fake.call_count, 0);
	zassert_equal(hw_adc_init_fake.call_count, 0);
	zassert_equal(hw_motor_stop_fake.call_count, 0);
}

ZTEST(controller, test_init_erro_no_display_para_antes_do_adc)
{
	fakes_reset();
	st7789_start_fake.return_val = -ENOMEM;

	zassert_equal(controller_init(), -ENOMEM);
	zassert_equal(hw_uart_init_fake.call_count, 1);
	zassert_equal(hw_adc_init_fake.call_count, 0);
	zassert_equal(hw_motor_stop_fake.call_count, 0);
}

ZTEST(controller, test_init_zera_o_display_entregue)
{
	/* Display ja concluiu o pedido 3: a saida recebe EV_DISPLAY uma vez. */
	mundo.disp_done = 3U;
	passo();
	zassert_equal(st7789_failed_fake.call_count, 1);
	passo();
	zassert_equal(st7789_failed_fake.call_count, 1, "ja entregue");

	/* Depois do init o controlador nao lembra o 3: entrega de novo. */
	zassert_equal(controller_init(), 0);
	passo();
	zassert_equal(st7789_failed_fake.call_count, 2,
		      "display_seen voltou a 0");

	/* Com o display em 0 depois do init, nada a entregar. */
	zassert_equal(controller_init(), 0);
	mundo.disp_done = 0U;
	passo();
	zassert_equal(st7789_failed_fake.call_count, 2);
}

ZTEST(controller, test_init_erro_no_adc_nao_impede_a_partida)
{
	/* A falha fica travada em hw_adc.c e aparece no primeiro poll. */
	fakes_reset();
	hw_adc_init_fake.return_val = -EIO;

	zassert_equal(controller_init(), 0);
	zassert_equal(hw_motor_stop_fake.call_count, 2);
	zassert_equal(faixa(LANE_ENTRY)->state, LANE_IDLE);
	zassert_equal(faixa(LANE_EXIT)->state, LANE_IDLE);
}

/* ---- controller_run ------------------------------------------------------ */

ZTEST(controller, test_run_devolve_o_erro_do_init_sem_servir)
{
	fakes_reset();
	hw_io_init_fake.return_val = -EIO;

	zassert_equal(controller_run(), -EIO);
	zassert_equal(hw_adc_init_fake.call_count, 0);
	zassert_equal(hw_adc_poll_fake.call_count, 0, "nenhum tick servido");
	zassert_equal(hw_limits_read_fake.call_count, 0);
}

ZTEST(controller, test_run_devolve_o_erro_do_display)
{
	fakes_reset();
	st7789_start_fake.return_val = -ENOMEM;

	zassert_equal(controller_run(), -ENOMEM);
	zassert_equal(hw_adc_poll_fake.call_count, 0);
}

/* ---- controller_step: ADC4 ------------------------------------------------ */

ZTEST(controller, test_step_partida_le_os_fins_de_curso_das_duas_faixas)
{
	passo();

	zassert_equal(hw_adc_poll_fake.call_count, 1);
	zassert_equal(hw_adc_poll_fake.arg0_val, agora);
	zassert_not_null(hw_adc_poll_fake.arg1_val);
	zassert_equal(hw_limits_read_fake.call_count, 2);
	zassert_equal(hw_limits_read_fake.arg0_history[0], entry);
	zassert_equal(hw_limits_read_fake.arg0_history[1], exit_cfg);
	for (int i = 0; i < LANE_COUNT; i++) {
		zassert_true(faixa(i)->started);
		zassert_true(faixa(i)->lower);
		zassert_false(faixa(i)->upper);
		zassert_equal(faixa(i)->state, LANE_IDLE);
		zassert_false(faixa(i)->have_code, "rc 0: nada novo");
	}
}

ZTEST(controller, test_step_falha_do_adc_trava_as_duas_faixas)
{
	passo();
	mundo.adc_rc = -EIO;
	passo();

	zassert_equal(faixa(LANE_ENTRY)->state, LANE_LOCKED);
	zassert_equal(faixa(LANE_EXIT)->state, LANE_LOCKED);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 2, "um pulso por faixa");
	zassert_equal(hw_buzzer_pulse_fake.arg0_history[0], entry);
	zassert_equal(hw_buzzer_pulse_fake.arg0_history[1], exit_cfg);
	zassert_false(faixa(LANE_ENTRY)->have_code);

	/* Travada ate o reset: o ADC voltar nao destrava. */
	mundo.adc_rc = 0;
	passo_com_amostra(LANE_ENTRY, LONGE);
	zassert_equal(faixa(LANE_ENTRY)->state, LANE_LOCKED);
	zassert_equal(faixa(LANE_EXIT)->state, LANE_LOCKED);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 2);
}

ZTEST(controller, test_step_amostra_vai_para_a_saida)
{
	passo_com_amostra(LANE_EXIT, 1234);

	zassert_true(faixa(LANE_EXIT)->have_code);
	zassert_equal(faixa(LANE_EXIT)->code, 1234);
	zassert_equal(faixa(LANE_EXIT)->code_ms, agora);
	zassert_false(faixa(LANE_ENTRY)->have_code, "a entrada nao recebe");
}

ZTEST(controller, test_step_amostra_vai_para_a_entrada)
{
	passo_com_amostra(LANE_ENTRY, 321);

	zassert_true(faixa(LANE_ENTRY)->have_code);
	zassert_equal(faixa(LANE_ENTRY)->code, 321);
	zassert_false(faixa(LANE_EXIT)->have_code, "a saida nao recebe");
}

/* ---- controller_step: fins de curso -------------------------------------- */

ZTEST(controller, test_step_erro_nos_fins_de_curso_trava_so_a_faixa)
{
	mundo.limits_rc[LANE_EXIT] = -EIO;
	passo();

	zassert_equal(faixa(LANE_EXIT)->state, LANE_LOCKED);
	zassert_equal(faixa(LANE_ENTRY)->state, LANE_IDLE);
	zassert_true(faixa(LANE_ENTRY)->started);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 1);
	zassert_equal(hw_buzzer_pulse_fake.arg0_val, exit_cfg);
	zassert_false(faixa(LANE_EXIT)->started,
		      "EV_HW_ERROR nao classifica a partida");
}

ZTEST(controller, test_step_fins_de_curso_entregues_a_cada_faixa)
{
	mundo.upper[LANE_ENTRY] = true;
	mundo.lower[LANE_ENTRY] = false;
	passo();

	/* Entrada aberta na partida: assistencia; saida fechada: IDLE. */
	zassert_true(faixa(LANE_ENTRY)->upper);
	zassert_false(faixa(LANE_ENTRY)->lower);
	zassert_equal(faixa(LANE_ENTRY)->state, LANE_ASSIST);
	zassert_equal(faixa(LANE_EXIT)->state, LANE_IDLE);
}

/* ---- controller_step: bordas do feixe ------------------------------------ */

ZTEST(controller, test_feixe_sem_bordas_nao_muda_nada)
{
	passo();
	zassert_equal(hw_beam_take_edges_fake.call_count, 2);
	zassert_equal(hw_beam_take_edges_fake.arg0_history[0], entry);
	zassert_equal(hw_beam_take_edges_fake.arg0_history[1], exit_cfg);
	zassert_false(faixa(LANE_ENTRY)->beam);
	zassert_false(faixa(LANE_EXIT)->beam);
}

ZTEST(controller, test_feixe_so_subida_ativa_a_faixa_certa)
{
	mundo.edges[LANE_EXIT] = HW_BEAM_ROSE | HW_BEAM_LEVEL;
	passo();

	zassert_true(faixa(LANE_EXIT)->beam);
	zassert_false(faixa(LANE_ENTRY)->beam);
}

ZTEST(controller, test_feixe_so_descida_desativa)
{
	mundo.edges[LANE_ENTRY] = HW_BEAM_ROSE | HW_BEAM_LEVEL;
	passo();
	zassert_true(faixa(LANE_ENTRY)->beam);

	mundo.edges[LANE_ENTRY] = HW_BEAM_FELL;
	passo();
	zassert_false(faixa(LANE_ENTRY)->beam);
}

ZTEST(controller, test_feixe_subida_e_descida_terminando_ativo)
{
	/* Saiu e voltou entre dois ticks: OFF, depois ON. */
	mundo.edges[LANE_ENTRY] = HW_BEAM_ROSE | HW_BEAM_LEVEL;
	passo();
	mundo.edges[LANE_ENTRY] = HW_BEAM_ROSE | HW_BEAM_FELL | HW_BEAM_LEVEL;
	passo();
	zassert_true(faixa(LANE_ENTRY)->beam, "termina ativo");
}

ZTEST(controller, test_feixe_subida_e_descida_terminando_inativo)
{
	/* Entrou e saiu entre dois ticks: ON, depois OFF. */
	mundo.edges[LANE_ENTRY] = HW_BEAM_ROSE | HW_BEAM_FELL;
	passo();
	zassert_false(faixa(LANE_ENTRY)->beam, "termina inativo");
}

ZTEST(controller, test_feixe_on_off_no_mesmo_tick_completa_a_passagem)
{
	entrada_aberta();

	/* Carro passou inteiro entre dois ticks e ja esta longe. */
	hw_motor_drive_fake.call_count = 0;
	mundo.edges[LANE_ENTRY] = HW_BEAM_ROSE | HW_BEAM_FELL;
	passo_com_amostra(LANE_ENTRY, LONGE);

	zassert_false(faixa(LANE_ENTRY)->beam);
	zassert_equal(faixa(LANE_ENTRY)->state, LANE_CLOSING,
		      "ON depois OFF: passagem completa");
	zassert_true(sessions_has(PLACA), "a passagem registra a sessao");
	zassert_equal(hw_motor_drive_fake.call_count, 1);
	zassert_equal(hw_motor_drive_fake.arg0_val, entry);
	zassert_false(hw_motor_drive_fake.arg1_val, "fecha");
}

ZTEST(controller, test_feixe_off_on_no_mesmo_tick_nao_completa_a_passagem)
{
	entrada_aberta();

	/* Na ordem OFF, ON o feixe termina ocupado: ainda ha carro. */
	mundo.edges[LANE_ENTRY] = HW_BEAM_ROSE | HW_BEAM_FELL | HW_BEAM_LEVEL;
	passo_com_amostra(LANE_ENTRY, LONGE);

	zassert_true(faixa(LANE_ENTRY)->beam);
	zassert_true(faixa(LANE_ENTRY)->beam_seen);
	zassert_false(faixa(LANE_ENTRY)->beam_cleared, "o ON veio por ultimo");
	zassert_equal(faixa(LANE_ENTRY)->state, LANE_WAIT_PASSAGE);
	zassert_false(sessions_has(PLACA));

	/* Quando o feixe libera, a passagem completa. */
	mundo.edges[LANE_ENTRY] = HW_BEAM_FELL;
	passo_com_amostra(LANE_ENTRY, LONGE);
	zassert_equal(faixa(LANE_ENTRY)->state, LANE_CLOSING);
	zassert_true(sessions_has(PLACA));
}

/* ---- controller_step: cameras --------------------------------------------- */

ZTEST(controller, test_camera_de_cada_faixa_e_consultada)
{
	passo();

	zassert_equal(hw_uart_poll_plate_fake.call_count, 2);
	zassert_equal(hw_uart_poll_plate_fake.arg0_history[0],
		      HW_UART_ENTRY_CAMERA);
	zassert_equal(hw_uart_poll_plate_fake.arg0_history[1],
		      HW_UART_EXIT_CAMERA);
}

ZTEST(controller, test_camera_entrega_todos_os_quadros_do_tick)
{
	uint32_t id;

	passo();
	chegar(LANE_ENTRY);
	zassert_equal(mundo.uart_port, HW_UART_ENTRY_CAMERA);
	zassert_mem_equal(mundo.uart_line, "AT+CAPTURE=", 11);

	/* Um +PLATE de ID antigo e o do ID atual no mesmo tick. */
	id = faixa(LANE_ENTRY)->capture_id;

	mundo_placa(HW_UART_ENTRY_CAMERA, id + 7U, "XYZ9Z99", 99);
	mundo_placa(HW_UART_ENTRY_CAMERA, id, PLACA, 93);
	hw_uart_poll_plate_fake.call_count = 0;
	hw_motor_drive_fake.call_count = 0;
	passo();

	zassert_equal(mundo.n_plates[HW_UART_ENTRY_CAMERA], 0, "fila vazia");
	/* Entrada: dois quadros e o falso; saida: um falso. */
	zassert_equal(hw_uart_poll_plate_fake.call_count, 4);
	zassert_equal(faixa(LANE_ENTRY)->state, LANE_OPENING);
	zassert_str_equal(faixa(LANE_ENTRY)->plate, PLACA);
	zassert_equal(hw_motor_drive_fake.call_count, 1);
	zassert_equal(hw_motor_drive_fake.arg0_val, entry);
	zassert_true(hw_motor_drive_fake.arg1_val, "abre");
}

ZTEST(controller, test_camera_da_saida_nao_alimenta_a_entrada)
{
	passo();
	chegar(LANE_ENTRY);

	/* O ID certo da entrada, mas chegando pela camera da saida. */
	mundo_placa(HW_UART_EXIT_CAMERA, faixa(LANE_ENTRY)->capture_id, PLACA,
		    93);
	passo();

	zassert_equal(mundo.n_plates[HW_UART_EXIT_CAMERA], 0, "foi consumido");
	zassert_equal(faixa(LANE_ENTRY)->state, LANE_IDENTIFYING);
	zassert_equal(faixa(LANE_EXIT)->state, LANE_IDLE);
	zassert_equal(hw_motor_drive_fake.call_count, 0);
}

/* ---- controller_step: terminal de pagamento ------------------------------ */

ZTEST(controller, test_pagamento_so_consultado_na_saida)
{
	int i_camera_saida = -1;
	int i_pagamento = -1;
	int n_camera = 0;

	passo();
	zassert_equal(hw_uart_poll_payment_fake.call_count, 1);

	/* A consulta vem no servico da saida, depois da camera da saida. */
	for (int i = 0; i < (int)fff.call_history_idx; i++) {
		if (fff.call_history[i] == (void *)hw_uart_poll_plate) {
			n_camera++;
			if (n_camera == 2) {
				i_camera_saida = i;
			}
		}
		if (fff.call_history[i] == (void *)hw_uart_poll_payment) {
			i_pagamento = i;
		}
	}
	zassert_true(i_camera_saida >= 0);
	zassert_true(i_pagamento > i_camera_saida);
}

ZTEST(controller, test_pagamento_entregue_a_saida_abre)
{
	saida_cobrando();
	zassert_equal(mundo.uart_port, HW_UART_PAYMENT);
	zassert_mem_equal(mundo.uart_line, "P1 CHARGE ", 10);

	/* Um resultado de outra cobranca e o PAID certo no mesmo tick. */
	mundo_pagamento(P1_STATUS_PAID, "00000000-0000-4000-8000-000000000000",
			faixa(LANE_EXIT)->cents);
	mundo_pagamento(P1_STATUS_PAID, faixa(LANE_EXIT)->uuid,
			faixa(LANE_EXIT)->cents);
	hw_uart_poll_payment_fake.call_count = 0;
	hw_motor_drive_fake.call_count = 0;
	passo();

	zassert_equal(mundo.n_payments, 0, "fila vazia");
	zassert_equal(hw_uart_poll_payment_fake.call_count, 3);
	zassert_equal(faixa(LANE_EXIT)->state, LANE_OPENING);
	zassert_equal(faixa(LANE_ENTRY)->state, LANE_IDLE);
	zassert_equal(hw_motor_drive_fake.call_count, 1);
	zassert_equal(hw_motor_drive_fake.arg0_val, exit_cfg);
	zassert_true(hw_motor_drive_fake.arg1_val, "abre");
}

/* ---- controller_step: display ---------------------------------------------- */

ZTEST(controller, test_display_consultado_so_na_saida_e_falha_depois)
{
	int i_camera_saida;
	int i_done;
	int i_failed;
	int i_pagamento;

	mundo.disp_done = 1U;
	passo();

	/* Uma consulta por tick, no servico da saida. */
	zassert_equal(st7789_done_fake.call_count, 1);
	zassert_equal(st7789_failed_fake.call_count, 1);
	i_done = posicao((void *)st7789_done);
	i_failed = posicao((void *)st7789_failed);
	i_pagamento = posicao((void *)hw_uart_poll_payment);
	i_camera_saida = -1;
	for (int i = 0, n = 0; i < (int)fff.call_history_idx; i++) {
		if (fff.call_history[i] == (void *)hw_uart_poll_plate &&
		    ++n == 2) {
			i_camera_saida = i;
		}
	}
	zassert_true(i_camera_saida >= 0);
	zassert_true(i_done > i_camera_saida, "depois da camera da saida");
	zassert_true(i_failed > i_done, "a falha e lida depois da conclusao");
	zassert_true(i_pagamento > i_failed, "antes do terminal");
}

ZTEST(controller, test_display_entregue_so_quando_muda)
{
	passo();
	zassert_equal(st7789_done_fake.call_count, 1);
	zassert_equal(st7789_failed_fake.call_count, 0, "done 0: nada mudou");

	mundo.disp_done = 1U;
	passo();
	zassert_equal(st7789_failed_fake.call_count, 1);

	passo();
	passo();
	zassert_equal(st7789_done_fake.call_count, 4);
	zassert_equal(st7789_failed_fake.call_count, 1, "mesmo done: sem evento");

	mundo.disp_done = 2U;
	passo();
	zassert_equal(st7789_failed_fake.call_count, 2);
}

ZTEST(controller, test_display_ignorado_fora_da_cobranca)
{
	/* EV_DISPLAY com a saida parada nao muda nada. */
	mundo.disp_done = 9U;
	passo();
	zassert_equal(st7789_failed_fake.call_count, 1);
	zassert_equal(faixa(LANE_EXIT)->state, LANE_IDLE);
	zassert_equal(faixa(LANE_ENTRY)->state, LANE_IDLE);
	zassert_equal(st7789_show_charge_fake.call_count, 0);
	zassert_equal(envios_ao_terminal(), 0);
}

ZTEST(controller, test_display_pedido_anterior_nao_avanca)
{
	/* O display ja recebeu 5 pedidos (de cobrancas anteriores). */
	mundo.disp_emitidos = 5U;
	saida_mostrando_placa();
	zassert_equal(faixa(LANE_EXIT)->disp_ticket, 6U);

	/* O display concluiu so um pedido anterior ao da placa. */
	mundo.disp_done = faixa(LANE_EXIT)->disp_ticket - 1U;
	passo();
	zassert_equal(st7789_failed_fake.call_count, 1, "entregue");
	zassert_equal(faixa(LANE_EXIT)->state, LANE_DISPLAYING);
	zassert_false(faixa(LANE_EXIT)->price_frozen);
	zassert_equal(st7789_show_charge_fake.call_count, 0);
}

ZTEST(controller, test_display_falha_na_placa_vai_para_assistencia)
{
	saida_mostrando_placa();

	mundo_display_falha();
	passo();
	zassert_equal(faixa(LANE_EXIT)->state, LANE_ASSIST);
	zassert_equal(st7789_show_charge_fake.call_count, 0, "sem valor");
	zassert_equal(faixa(LANE_EXIT)->uuid[0], '\0');
	zassert_equal(envios_ao_terminal(), 0, "sem P1 CHARGE");
	zassert_equal(faixa(LANE_ENTRY)->state, LANE_IDLE);
}

ZTEST(controller, test_display_falha_no_valor_vai_para_assistencia)
{
	saida_mostrando_placa();
	mundo_display_conclui();
	passo();
	zassert_true(faixa(LANE_EXIT)->price_frozen);

	mundo_display_falha();
	passo();
	zassert_equal(faixa(LANE_EXIT)->state, LANE_ASSIST);
	zassert_equal(faixa(LANE_EXIT)->uuid[0], '\0');
	zassert_equal(envios_ao_terminal(), 0, "sem P1 CHARGE");
}

ZTEST(controller, test_display_valor_concluido_envia_charge)
{
	saida_cobrando();

	zassert_equal(envios_ao_terminal(), 1);
	zassert_equal(mundo.uart_port, HW_UART_PAYMENT);
	zassert_mem_equal(mundo.uart_line, "P1 CHARGE ", 10);
	zassert_not_null(strstr(mundo.uart_line, faixa(LANE_EXIT)->uuid));
	zassert_equal(st7789_failed_fake.call_count, 2, "um por pedido");
}
