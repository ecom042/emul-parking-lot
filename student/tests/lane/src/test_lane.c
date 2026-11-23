/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Maquina de estados de uma faixa, com a camada de hardware falsa (FFF).
 * Cada teste monta a faixa com lane_init() e entrega eventos com tempo
 * virtual explicito, como faria controller.c a cada tick.
 */
#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "fakes.h"
#include "lane.h"
#include "sessions.h"

#define PERTO 500U  /* codigo de carro a ~730 mm: chegada */
#define LONGE 2000U /* codigo de carro a ~2900 mm: foi embora */

static struct lane lane;
static const struct lane_cfg *const entry = &lane_cfgs[LANE_ENTRY];
static const struct lane_cfg *const exit_cfg = &lane_cfgs[LANE_EXIT];

static void preparar(void *fixture)
{
	ARG_UNUSED(fixture);
	fakes_reset();
	sessions_reset();
}

ZTEST_SUITE(lane, NULL, NULL, preparar, NULL, NULL);

/* ---- Eventos ---------------------------------------------------------- */

static void ev_simples(enum lane_ev_id id, int64_t now)
{
	struct lane_event ev = { .id = id, .now_ms = now };

	lane_handle_event(&lane, &ev);
}

static void tick(int64_t now)
{
	ev_simples(EV_TICK, now);
}

static void ev_limites(bool upper, bool lower, int64_t now)
{
	struct lane_event ev = {
		.id = EV_LIMITS,
		.now_ms = now,
		.limits = { .upper = upper, .lower = lower },
	};

	lane_handle_event(&lane, &ev);
}

static void ev_distancia(uint16_t code, int64_t now)
{
	struct lane_event ev = { .id = EV_DISTANCE, .now_ms = now, .code = code };

	lane_handle_event(&lane, &ev);
}

static void ev_placa(uint32_t id, const char *plate, uint8_t conf, int64_t now)
{
	struct lane_event ev = { .id = EV_PLATE, .now_ms = now };

	ev.plate.id = id;
	strncpy(ev.plate.plate, plate, sizeof(ev.plate.plate) - 1);
	ev.plate.confidence = conf;
	lane_handle_event(&lane, &ev);
}

/* ---- Roteiros ---------------------------------------------------------- */

/** Partida com a cancela na posicao dada; o 1o EV_LIMITS classifica. */
static void iniciar(const struct lane_cfg *cfg, bool upper, bool lower)
{
	lane_init(&lane, cfg);
	ev_limites(upper, lower, 0);
}

/** ADC_DEBOUNCE_N amostras iguais. */
static void amostras(uint16_t code, int64_t now)
{
	for (int i = 0; i < ADC_DEBOUNCE_N; i++) {
		ev_distancia(code, now);
	}
}

/** Entrada fechada, carro chega e a placa boa chega: fica OPENING. */
static void ate_abrindo(int64_t now)
{
	iniciar(entry, false, true);
	amostras(PERTO, now);
	ev_placa(lane.capture_id, "ABC1D23", 93, now);
	zassert_equal(lane.state, LANE_OPENING);
}

/** Continua de OPENING ate a cancela aberta com verde: WAIT_PASSAGE. */
static void ate_aberta(int64_t now)
{
	ate_abrindo(now);
	ev_limites(true, false, now + 2000);
	tick(now + 2000);
	zassert_equal(lane.state, LANE_WAIT_PASSAGE);
}

/** Feixe ativo, depois inativo, carro longe: passagem completa. */
static void passar(int64_t now)
{
	ev_simples(EV_BEAM_ON, now);
	ev_simples(EV_BEAM_OFF, now + 100);
	ev_distancia(LONGE, now + 200);
	tick(now + 200);
}

/* ---- Partida (REQ-01) --------------------------------------------------- */

ZTEST(lane, test_partida_fechada_vai_para_idle)
{
	lane_init(&lane, entry);
	zassert_equal(hw_motor_stop_fake.call_count, 1);
	zassert_equal(hw_lights_set_fake.call_count, 1);
	zassert_true(hw_lights_set_fake.arg1_val, "vermelho aceso");
	zassert_false(hw_lights_set_fake.arg2_val, "verde apagado");
	zassert_false(lane.started);

	ev_limites(false, true, 10);
	zassert_true(lane.started);
	zassert_equal(lane.state, LANE_IDLE);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 0);
}

ZTEST(lane, test_partida_sem_inferior_pede_assistencia)
{
	iniciar(entry, false, false);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 1);
	zassert_equal(hw_motor_drive_fake.call_count, 0, "nunca fecha sozinha");
}

ZTEST(lane, test_partida_aberta_pede_assistencia)
{
	iniciar(entry, true, false);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(hw_motor_drive_fake.call_count, 0);
}

ZTEST(lane, test_partida_contraditoria_trava)
{
	iniciar(entry, true, true);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 1);
}

ZTEST(lane, test_partida_erro_no_motor_trava)
{
	hw_motor_stop_fake.return_val = -EIO;
	lane_init(&lane, entry);
	zassert_equal(lane.state, LANE_LOCKED);
}

ZTEST(lane, test_partida_erro_nas_luzes_trava)
{
	hw_lights_set_fake.return_val = -EIO;
	lane_init(&lane, entry);
	zassert_equal(lane.state, LANE_LOCKED);
}

ZTEST(lane, test_partida_so_classifica_uma_vez)
{
	iniciar(entry, false, true);
	/* Depois da partida, inferior baixo nao e mais "sem inferior". */
	ev_limites(false, false, 10);
	zassert_equal(lane.state, LANE_IDLE);
}

/* ---- Falhas travadas (REQ-01, REQ-09, REQ-10) --------------------------- */

ZTEST(lane, test_limites_contraditorios_travam_ate_o_reset)
{
	iniciar(entry, false, true);
	ev_limites(true, true, 10);
	zassert_equal(lane.state, LANE_LOCKED);

	/* A falha some, o carro vai embora: continua travada. */
	ev_limites(false, true, 20);
	ev_distancia(4000, 30);
	tick(40);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 1, "um so pulso");
}

ZTEST(lane, test_falha_do_adc_trava)
{
	iniciar(entry, false, true);
	ev_simples(EV_ADC_FAULT, 10);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_true(hw_motor_stop_fake.call_count >= 2);
}

ZTEST(lane, test_erro_de_driver_trava)
{
	iniciar(entry, false, true);
	ev_simples(EV_HW_ERROR, 10);
	zassert_equal(lane.state, LANE_LOCKED);
}

ZTEST(lane, test_assistencia_que_vira_travada_nao_toca_de_novo)
{
	iniciar(entry, false, false);
	zassert_equal(lane.state, LANE_ASSIST);

	ev_simples(EV_ADC_FAULT, 10);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 1);
}

/* ---- Rearme da assistencia (REQ-09) ------------------------------------ */

ZTEST(lane, test_assistencia_rearma_com_cancela_fechada_e_carro_longe)
{
	iniciar(entry, false, false);

	/* Ainda sem inferior: nao rearma. */
	ev_distancia(ADC_LEAVE + 1, 10);
	tick(10);
	zassert_equal(lane.state, LANE_ASSIST);

	ev_limites(false, true, 20);
	ev_distancia(ADC_LEAVE + 1, 20);
	tick(20);
	zassert_equal(lane.state, LANE_IDLE);
}

ZTEST(lane, test_assistencia_nao_rearma_com_carro_perto)
{
	iniciar(entry, false, false);
	ev_limites(false, true, 10);
	ev_distancia(ADC_LEAVE, 10);
	tick(10);
	zassert_equal(lane.state, LANE_ASSIST);
}

ZTEST(lane, test_assistencia_nao_rearma_com_amostra_velha)
{
	iniciar(entry, false, false);
	/* Carro longe, mas a amostra envelhece antes de a cancela fechar. */
	ev_distancia(ADC_LEAVE + 1, 10);
	ev_limites(false, true, 10 + ADC_FRESH_MS);
	zassert_equal(lane.state, LANE_ASSIST);
}

ZTEST(lane, test_assistencia_nao_rearma_com_feixe_ocupado)
{
	iniciar(entry, false, false);
	ev_simples(EV_BEAM_ON, 5);
	ev_limites(false, true, 10);
	ev_distancia(ADC_LEAVE + 1, 10);
	zassert_equal(lane.state, LANE_ASSIST);

	ev_simples(EV_BEAM_OFF, 15);
	zassert_equal(lane.state, LANE_IDLE);
}

ZTEST(lane, test_assistencia_nao_rearma_com_motor_ligado)
{
	iniciar(entry, false, false);
	hw_motor_running_fake.return_val = true;
	ev_limites(false, true, 10);
	ev_distancia(ADC_LEAVE + 1, 10);
	zassert_equal(lane.state, LANE_ASSIST);
}

ZTEST(lane, test_assistencia_nao_rearma_sem_amostra)
{
	iniciar(entry, false, false);
	ev_limites(false, true, 10);
	tick(10);
	zassert_equal(lane.state, LANE_ASSIST);
}

/* ---- Identificacao (REQ-02, REQ-03) ------------------------------------- */

ZTEST(lane, test_chegada_inicia_captura)
{
	iniciar(entry, false, true);
	ev_distancia(PERTO, 100);
	ev_distancia(PERTO, 120);
	zassert_equal(hw_uart_send_fake.call_count, 0, "debounce de 3");

	ev_distancia(PERTO, 140);
	zassert_equal(lane.state, LANE_IDENTIFYING);
	zassert_equal(hw_uart_send_fake.call_count, 1);
	zassert_equal(hw_uart_send_fake.arg0_val, HW_UART_ENTRY_CAMERA);
	zassert_str_equal(fake_uart_line, "AT+CAPTURE=1");
	zassert_equal(lane.tries, 1);
	zassert_equal(lane.deadline_ms, 140 + CAPTURE_MS);
}

ZTEST(lane, test_chegada_sem_cancela_fechada_nao_captura)
{
	iniciar(entry, false, true);
	hw_motor_running_fake.return_val = true;
	amostras(PERTO, 100);
	zassert_equal(lane.state, LANE_IDLE);
	zassert_equal(hw_uart_send_fake.call_count, 0);
}

ZTEST(lane, test_presenca_com_amostra_velha_nao_captura)
{
	iniciar(entry, false, true);
	/* O carro chega enquanto o motor ainda roda... */
	hw_motor_running_fake.return_val = true;
	amostras(PERTO, 100);
	/* ...e o motor para quando a amostra ja envelheceu. */
	hw_motor_running_fake.return_val = false;
	ev_limites(false, true, 100 + ADC_FRESH_MS);
	zassert_equal(hw_uart_send_fake.call_count, 0);

	ev_distancia(PERTO, 100 + ADC_FRESH_MS);
	zassert_equal(lane.state, LANE_IDENTIFYING);
}

ZTEST(lane, test_placa_de_outro_id_e_ignorada)
{
	iniciar(entry, false, true);
	amostras(PERTO, 100);
	ev_placa(lane.capture_id + 1, "ABC1D23", 93, 150);
	zassert_equal(lane.state, LANE_IDENTIFYING);
	zassert_equal(hw_motor_drive_fake.call_count, 0);
}

ZTEST(lane, test_outros_eventos_nao_mudam_a_identificacao)
{
	iniciar(entry, false, true);
	amostras(PERTO, 100);
	ev_simples(EV_BEAM_ON, 150);
	zassert_equal(lane.state, LANE_IDENTIFYING);
}

ZTEST(lane, test_confianca_baixa_gasta_tentativa)
{
	iniciar(entry, false, true);
	amostras(PERTO, 100);
	ev_placa(1, "ABC1D23", CONF_MIN - 1, 200);
	zassert_equal(lane.state, LANE_IDENTIFYING);
	zassert_equal(lane.tries, 2);
	zassert_str_equal(fake_uart_line, "AT+CAPTURE=2");
	zassert_equal(lane.deadline_ms, 200 + CAPTURE_MS);

	/* A resposta antiga nao vale mais. */
	ev_placa(1, "ABC1D23", 93, 210);
	zassert_equal(lane.state, LANE_IDENTIFYING);
}

ZTEST(lane, test_confianca_85_e_aceita)
{
	iniciar(entry, false, true);
	amostras(PERTO, 100);
	ev_placa(1, "ABC1D23", CONF_MIN, 200);
	zassert_equal(lane.state, LANE_OPENING);
	zassert_equal(hw_motor_drive_fake.call_count, 1);
	zassert_true(hw_motor_drive_fake.arg1_val, "abre");
	zassert_equal(lane.deadline_ms, 200 + GATE_MOVE_MS);
	zassert_str_equal(lane.plate, "ABC1D23");
	zassert_equal(lane.capture_id, 0, "ID invalidado");
}

ZTEST(lane, test_tres_confiancas_baixas_pedem_assistencia)
{
	iniciar(entry, false, true);
	amostras(PERTO, 100);
	ev_placa(1, "ABC1D23", 62, 200);
	ev_placa(2, "ABC1D23", 79, 300);
	ev_placa(3, "ABC1D23", 84, 400);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(hw_uart_send_fake.call_count, 3);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 1);

	/* Carro parado continua presente: nenhuma nova captura. */
	amostras(PERTO, 500);
	tick(600);
	zassert_equal(hw_uart_send_fake.call_count, 3);
}

ZTEST(lane, test_prazo_da_captura)
{
	iniciar(entry, false, true);
	amostras(PERTO, 100);
	tick(100 + CAPTURE_MS - 10);
	zassert_equal(lane.tries, 1, "antes do prazo nada muda");

	tick(100 + CAPTURE_MS);
	zassert_equal(lane.tries, 2);
	zassert_str_equal(fake_uart_line, "AT+CAPTURE=2");
	tick(100 + 2 * CAPTURE_MS);
	tick(100 + 3 * CAPTURE_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(hw_uart_send_fake.call_count, 3);
}

ZTEST(lane, test_falha_de_envio_conta_como_tentativa)
{
	fake_uart_rc = -EBUSY;
	iniciar(entry, false, true);
	amostras(PERTO, 100);
	zassert_equal(lane.state, LANE_IDENTIFYING);
	tick(100 + CAPTURE_MS);
	zassert_equal(lane.tries, 2);
}

ZTEST(lane, test_carro_que_sai_abandona_a_identificacao)
{
	iniciar(entry, false, true);
	amostras(PERTO, 100);
	ev_distancia(LONGE, 150);
	tick(150);
	zassert_equal(lane.state, LANE_IDLE);
	zassert_equal(lane.capture_id, 0);

	/* A resposta atrasada nao autoriza nada. */
	ev_placa(1, "ABC1D23", 93, 200);
	zassert_equal(lane.state, LANE_IDLE);
	zassert_equal(hw_motor_drive_fake.call_count, 0);
}

ZTEST(lane, test_novo_carro_depois_que_o_anterior_saiu)
{
	iniciar(entry, false, true);
	amostras(PERTO, 100);
	ev_placa(1, "ABC1D23", 10, 110);
	ev_placa(2, "ABC1D23", 10, 120);
	ev_placa(3, "ABC1D23", 10, 130);
	zassert_equal(lane.state, LANE_ASSIST);

	amostras(LONGE, 200);
	tick(200);
	zassert_equal(lane.state, LANE_IDLE);
	zassert_false(lane.served);

	amostras(PERTO, 300);
	zassert_equal(lane.state, LANE_IDENTIFYING);
	zassert_str_equal(fake_uart_line, "AT+CAPTURE=4", "ID sempre novo");
}

ZTEST(lane, test_id_de_captura_volta_a_1)
{
	iniciar(entry, false, true);
	lane.id_counter = 999999999U;
	amostras(PERTO, 100);
	zassert_str_equal(fake_uart_line, "AT+CAPTURE=1");
}

/* ---- Sessoes na entrada e na saida (REQ-04) ----------------------------- */

ZTEST(lane, test_lotado_pede_assistencia)
{
	static const char *const placas[] = {
		"AAA1A11", "BBB2B22", "CCC3C33", "DDD4D44",
		"EEE5E55", "FFF6F66", "GGG7G77", "HHH8H88",
	};

	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		zassert_ok(sessions_add(placas[i], 0));
	}
	iniciar(entry, false, true);
	amostras(PERTO, 100);
	ev_placa(1, "ABC1D23", 93, 200);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(hw_motor_drive_fake.call_count, 0);
}

ZTEST(lane, test_placa_ja_estacionada_pede_assistencia)
{
	zassert_ok(sessions_add("ABC1D23", 0));
	iniciar(entry, false, true);
	amostras(PERTO, 100);
	ev_placa(1, "ABC1D23", 93, 200);
	zassert_equal(lane.state, LANE_ASSIST);
}

ZTEST(lane, test_saida_sem_sessao_pede_assistencia)
{
	iniciar(exit_cfg, false, true);
	amostras(PERTO, 100);
	zassert_equal(hw_uart_send_fake.arg0_val, HW_UART_EXIT_CAMERA);
	ev_placa(1, "ABC1D23", 93, 200);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(hw_motor_drive_fake.call_count, 0);
}

/* ---- Abertura (REQ-06) -------------------------------------------------- */

ZTEST(lane, test_erro_ao_ligar_o_motor_trava)
{
	hw_motor_drive_fake.return_val = -EIO;
	iniciar(entry, false, true);
	amostras(PERTO, 100);
	ev_placa(1, "ABC1D23", 93, 200);
	zassert_equal(lane.state, LANE_LOCKED);
}

ZTEST(lane, test_verde_so_no_superior_com_motor_desligado)
{
	ate_abrindo(100);
	tick(500);
	zassert_equal(lane.state, LANE_OPENING, "sem superior, segue abrindo");

	ev_limites(true, false, 2100);
	tick(2100);
	zassert_equal(lane.state, LANE_WAIT_PASSAGE);
	zassert_false(hw_lights_set_fake.arg1_val, "vermelho apagado");
	zassert_true(hw_lights_set_fake.arg2_val, "verde aceso");
	zassert_equal(lane.deadline_ms, 2100 + PASS_MS);
}

ZTEST(lane, test_superior_com_motor_ligado_desliga_antes_do_verde)
{
	static bool seq[] = { true, false };

	ate_abrindo(100);
	SET_RETURN_SEQ(hw_motor_running, seq, 2);
	ev_limites(true, false, 2100);
	tick(2100);
	zassert_equal(hw_motor_stop_fake.call_count, 2, "partida + aqui");
	zassert_equal(lane.state, LANE_WAIT_PASSAGE);
}

ZTEST(lane, test_motor_que_nao_desliga_nao_acende_verde)
{
	ate_abrindo(100);
	hw_motor_running_fake.return_val = true;
	ev_limites(true, false, 2100);
	tick(2100);
	zassert_equal(lane.state, LANE_OPENING);

	tick(100 + GATE_MOVE_MS);
	zassert_equal(lane.state, LANE_LOCKED);
}

ZTEST(lane, test_erro_ao_desligar_no_superior_trava)
{
	ate_abrindo(100);
	hw_motor_running_fake.return_val = true;
	hw_motor_stop_fake.return_val = -EIO;
	ev_limites(true, false, 2100);
	tick(2100);
	zassert_equal(lane.state, LANE_LOCKED);
}

ZTEST(lane, test_erro_ao_acender_o_verde_trava)
{
	ate_abrindo(100);
	hw_lights_set_fake.return_val = -EIO;
	ev_limites(true, false, 2100);
	tick(2100);
	zassert_equal(lane.state, LANE_LOCKED);
}

ZTEST(lane, test_prazo_de_abertura_trava)
{
	ate_abrindo(100);
	ev_simples(EV_BEAM_ON, 200); /* nao e tick: ignorado */
	tick(100 + GATE_MOVE_MS - 10);
	zassert_equal(lane.state, LANE_OPENING);
	tick(100 + GATE_MOVE_MS);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 1);
}

/* ---- Passagem (REQ-07) -------------------------------------------------- */

ZTEST(lane, test_passagem_completa_cria_sessao_e_fecha)
{
	ate_aberta(100);
	passar(3000);
	zassert_true(sessions_has("ABC1D23"));
	zassert_equal(lane.state, LANE_CLOSING);
	zassert_true(hw_lights_set_fake.arg1_val, "vermelho antes de fechar");
	zassert_false(hw_lights_set_fake.arg2_val, "verde apagado");
	zassert_false(hw_motor_drive_fake.arg1_val, "fecha");
	zassert_equal(lane.deadline_ms, 3200 + GATE_MOVE_MS);

	int64_t entrada = 0;

	zassert_ok(sessions_entry_time("ABC1D23", &entrada));
	zassert_equal(entrada, 3200, "a tarifa conta da passagem");
}

ZTEST(lane, test_sem_feixe_nao_ha_passagem)
{
	ate_aberta(100);
	ev_distancia(LONGE, 3000);
	tick(3000);
	zassert_equal(lane.state, LANE_WAIT_PASSAGE);
	zassert_false(sessions_has("ABC1D23"));
}

ZTEST(lane, test_feixe_inativo_sem_ativo_nao_conta)
{
	ate_aberta(100);
	ev_simples(EV_BEAM_OFF, 3000);
	ev_distancia(LONGE, 3000);
	tick(3000);
	zassert_equal(lane.state, LANE_WAIT_PASSAGE);
}

ZTEST(lane, test_feixe_que_volta_zera_a_passagem)
{
	ate_aberta(100);
	ev_simples(EV_BEAM_ON, 3000);
	ev_simples(EV_BEAM_OFF, 3100);
	ev_simples(EV_BEAM_ON, 3200);
	ev_distancia(LONGE, 3300);
	tick(3300);
	zassert_equal(lane.state, LANE_WAIT_PASSAGE, "feixe ocupado");

	ev_simples(EV_BEAM_OFF, 3400);
	ev_distancia(LONGE, 3400);
	tick(3400);
	zassert_equal(lane.state, LANE_CLOSING);
}

ZTEST(lane, test_carro_perto_nao_completa_a_passagem)
{
	ate_aberta(100);
	ev_simples(EV_BEAM_ON, 3000);
	ev_simples(EV_BEAM_OFF, 3100);
	ev_distancia(ADC_LEAVE, 3200);
	tick(3200);
	zassert_equal(lane.state, LANE_WAIT_PASSAGE);
}

ZTEST(lane, test_eventos_sem_efeito_na_passagem)
{
	ate_aberta(100);
	ev_placa(9, "ABC1D23", 93, 3000);
	ev_limites(true, false, 3000);
	zassert_equal(lane.state, LANE_WAIT_PASSAGE);
}

ZTEST(lane, test_prazo_da_passagem_pede_assistencia_aberta)
{
	ate_aberta(100);
	ev_simples(EV_BEAM_ON, 3000); /* obstrucao segurada */
	tick(2100 + PASS_MS);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(hw_motor_drive_fake.call_count, 1, "nunca fecha no feixe");
	zassert_false(sessions_has("ABC1D23"));
}

ZTEST(lane, test_sessao_que_falha_na_passagem_pede_assistencia)
{
	ate_aberta(100);
	/* A mesma placa entrou por outro caminho enquanto a cancela abria. */
	zassert_ok(sessions_add("ABC1D23", 0));
	passar(3000);
	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(hw_motor_drive_fake.call_count, 1);
}

ZTEST(lane, test_erro_nas_luzes_antes_de_fechar_trava)
{
	ate_aberta(100);
	hw_lights_set_fake.return_val = -EIO;
	passar(3000);
	zassert_equal(lane.state, LANE_LOCKED);
}

ZTEST(lane, test_erro_ao_fechar_trava)
{
	ate_aberta(100);
	hw_motor_drive_fake.return_val = -EIO;
	passar(3000);
	zassert_equal(lane.state, LANE_LOCKED);
}

/* ---- Fechamento (REQ-08) ------------------------------------------------ */

ZTEST(lane, test_fecha_no_inferior_e_volta_a_idle)
{
	ate_aberta(100);
	passar(3000);
	tick(3500);
	zassert_equal(lane.state, LANE_CLOSING);

	ev_limites(false, true, 5300);
	tick(5300);
	zassert_equal(lane.state, LANE_IDLE);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 0);
}

ZTEST(lane, test_obstrucao_no_fechamento_trava)
{
	ate_aberta(100);
	passar(3000);
	ev_simples(EV_BEAM_ON, 3500);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 1);

	/* O feixe libera: nada volta a mover. */
	ev_simples(EV_BEAM_OFF, 3600);
	ev_limites(false, false, 3700);
	tick(3700);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(hw_motor_drive_fake.call_count, 2);
}

ZTEST(lane, test_prazo_de_fechamento_trava)
{
	ate_aberta(100);
	passar(3000);
	ev_distancia(LONGE, 3300); /* nao e tick: ignorado */
	tick(3200 + GATE_MOVE_MS);
	zassert_equal(lane.state, LANE_LOCKED);
}

ZTEST(lane, test_inferior_com_motor_ligado_desliga_aqui)
{
	static bool seq[] = { true, false };

	ate_aberta(100);
	passar(3000);
	SET_RETURN_SEQ(hw_motor_running, seq, 2);
	ev_limites(false, true, 5300);
	tick(5300);
	zassert_equal(lane.state, LANE_IDLE);
}

ZTEST(lane, test_inferior_com_motor_que_nao_desliga_espera)
{
	ate_aberta(100);
	passar(3000);
	hw_motor_running_fake.return_val = true;
	ev_limites(false, true, 5300);
	tick(5300);
	zassert_equal(lane.state, LANE_CLOSING);
}

ZTEST(lane, test_erro_ao_desligar_no_inferior_trava)
{
	ate_aberta(100);
	passar(3000);
	hw_motor_running_fake.return_val = true;
	hw_motor_stop_fake.return_val = -EIO;
	ev_limites(false, true, 5300);
	tick(5300);
	zassert_equal(lane.state, LANE_LOCKED);
}
