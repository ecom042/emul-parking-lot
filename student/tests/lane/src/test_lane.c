/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Maquina de estados de uma faixa, com a camada de hardware falsa (FFF).
 * Cada teste monta a faixa com lane_init() e entrega eventos com tempo
 * virtual explicito.
 */
#include <errno.h>

#include <zephyr/ztest.h>

#include "fakes.h"
#include "lane.h"
#include "sessions.h"

static struct lane lane;
static const struct lane_cfg *const entry = &lane_cfgs[LANE_ENTRY];

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

/* ---- Partida (REQ-01) --------------------------------------------------- */

ZTEST(lane, test_partida_fechada_vai_para_idle)
{
	lane_init(&lane, entry);

	zassert_equal(lane.state, LANE_IDLE);
	zassert_equal(hw_motor_stop_fake.call_count, 1);
	zassert_equal(hw_lights_set_fake.call_count, 1);
	zassert_true(hw_lights_set_fake.arg1_val, "vermelho aceso");
	zassert_false(hw_lights_set_fake.arg2_val, "verde apagado");
	zassert_equal(hw_buzzer_pulse_fake.call_count, 0);
}

ZTEST(lane, test_partida_sem_inferior_pede_assistencia)
{
	fake_lower = false;
	lane_init(&lane, entry);

	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 1);
	zassert_equal(hw_motor_drive_fake.call_count, 0, "nunca fecha sozinha");
}

ZTEST(lane, test_partida_aberta_pede_assistencia)
{
	fake_upper = true;
	fake_lower = false;
	lane_init(&lane, entry);

	zassert_equal(lane.state, LANE_ASSIST);
	zassert_equal(hw_motor_drive_fake.call_count, 0);
}

ZTEST(lane, test_partida_contraditoria_trava)
{
	fake_upper = true;
	lane_init(&lane, entry);

	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 1);
}

ZTEST(lane, test_partida_com_erro_de_driver_trava)
{
	hw_limits_read_fake.custom_fake = NULL;
	hw_limits_read_fake.return_val = -EIO;
	lane_init(&lane, entry);

	zassert_equal(lane.state, LANE_LOCKED);
}

ZTEST(lane, test_partida_erro_no_motor_trava)
{
	hw_motor_stop_fake.return_val = -EIO;
	lane_init(&lane, entry);

	zassert_equal(lane.state, LANE_LOCKED);
}

/* ---- Falhas travadas (REQ-01, REQ-09, REQ-10) --------------------------- */

ZTEST(lane, test_limites_contraditorios_travam_ate_o_reset)
{
	lane_init(&lane, entry);
	ev_limites(true, true, 10);
	zassert_equal(lane.state, LANE_LOCKED);

	/* A falha some, o carro vai embora: continua travada. */
	ev_limites(false, true, 20);
	ev_distancia(4000, 30);
	ev_simples(EV_TICK, 40);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 1, "um so pulso");
}

ZTEST(lane, test_falha_do_adc_trava)
{
	lane_init(&lane, entry);
	ev_simples(EV_ADC_FAULT, 10);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_true(hw_motor_stop_fake.call_count >= 2);
}

ZTEST(lane, test_erro_de_driver_trava)
{
	lane_init(&lane, entry);
	ev_simples(EV_HW_ERROR, 10);
	zassert_equal(lane.state, LANE_LOCKED);
}

ZTEST(lane, test_assistencia_que_vira_travada_nao_toca_de_novo)
{
	fake_lower = false;
	lane_init(&lane, entry);
	zassert_equal(lane.state, LANE_ASSIST);

	ev_simples(EV_ADC_FAULT, 10);
	zassert_equal(lane.state, LANE_LOCKED);
	zassert_equal(hw_buzzer_pulse_fake.call_count, 1);
}

/* ---- Rearme da assistencia (REQ-09) ------------------------------------ */

ZTEST(lane, test_assistencia_rearma_com_cancela_fechada_e_carro_longe)
{
	fake_lower = false;
	lane_init(&lane, entry);
	zassert_equal(lane.state, LANE_ASSIST);

	/* Ainda sem inferior: nao rearma. */
	ev_distancia(ADC_LEAVE + 1, 10);
	ev_simples(EV_TICK, 10);
	zassert_equal(lane.state, LANE_ASSIST);

	ev_limites(false, true, 20);
	ev_distancia(ADC_LEAVE + 1, 20);
	ev_simples(EV_TICK, 20);
	zassert_equal(lane.state, LANE_IDLE);
}

ZTEST(lane, test_assistencia_nao_rearma_com_carro_perto)
{
	fake_lower = false;
	lane_init(&lane, entry);
	ev_limites(false, true, 10);
	ev_distancia(ADC_LEAVE, 10);
	ev_simples(EV_TICK, 10);
	zassert_equal(lane.state, LANE_ASSIST);
}

ZTEST(lane, test_assistencia_nao_rearma_com_amostra_velha)
{
	fake_lower = false;
	lane_init(&lane, entry);
	/* Carro longe, mas a amostra envelhece antes de a cancela fechar. */
	ev_distancia(ADC_LEAVE + 1, 10);
	ev_limites(false, true, 10 + ADC_FRESH_MS);
	zassert_equal(lane.state, LANE_ASSIST);
}

ZTEST(lane, test_assistencia_nao_rearma_com_feixe_ocupado)
{
	fake_lower = false;
	lane_init(&lane, entry);
	ev_simples(EV_BEAM_ON, 5);
	ev_limites(false, true, 10);
	ev_distancia(ADC_LEAVE + 1, 10);
	zassert_equal(lane.state, LANE_ASSIST);

	ev_simples(EV_BEAM_OFF, 15);
	zassert_equal(lane.state, LANE_IDLE);
}

ZTEST(lane, test_assistencia_nao_rearma_com_motor_ligado)
{
	fake_lower = false;
	lane_init(&lane, entry);
	hw_motor_running_fake.return_val = true;
	ev_limites(false, true, 10);
	ev_distancia(ADC_LEAVE + 1, 10);
	zassert_equal(lane.state, LANE_ASSIST);
}

ZTEST(lane, test_assistencia_nao_rearma_sem_amostra)
{
	fake_lower = false;
	lane_init(&lane, entry);
	ev_limites(false, true, 10);
	ev_simples(EV_TICK, 10);
	zassert_equal(lane.state, LANE_ASSIST);
}
