/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file hw.h
 * @brief Camada fina de GPIO de uma faixa: motor, sinais, buzzer, feixe e
 *        fins de curso.
 *
 * Implementado em hw_io.c, o unico arquivo que conhece os pinos. A logica
 * (lane.c, controller.c) usa apenas estas funcoes, para poder ser testada com
 * FFF.
 *
 * Duas reacoes de seguranca moram aqui, perto da interrupcao:
 * - fim de curso acionado (borda de subida): a ISR agenda um k_work que
 *   desliga o motor (o contrato manda adiar para o contexto de work);
 * - feixe ativo enquanto a faixa fecha: a propria ISR do feixe desliga o
 *   motor, para cumprir os 10 ms.
 *
 * As funcoes que devolvem int devolvem 0 em sucesso ou um errno negativo.
 */

#ifndef STUDENT_HW_H_
#define STUDENT_HW_H_

#include <stdbool.h>
#include <stdint.h>

struct lane_cfg;

/** Bordas do feixe registradas pela ISR desde a ultima consulta. */
#define HW_BEAM_ROSE  (1U << 0) /**< Ficou ativo pelo menos uma vez. */
#define HW_BEAM_FELL  (1U << 1) /**< Ficou inativo pelo menos uma vez. */
#define HW_BEAM_LEVEL (1U << 2) /**< Nivel na ultima borda: 1 = ativo. */

/**
 * @brief Configura os pinos e as interrupcoes das duas faixas.
 *
 * Saidas inativas, vermelho aceso. Fins de curso com interrupcao na subida,
 * feixe nas duas bordas.
 */
int hw_io_init(void);

/**
 * @brief Liga o motor no sentido pedido.
 *
 * Ao fechar, arma o corte pela ISR do feixe; se o feixe ja estiver ativo
 * depois de ligar, desliga na hora e registra a borda HW_BEAM_ROSE.
 *
 * @param open true abre (direcao alta), false fecha (direcao baixa).
 */
int hw_motor_drive(const struct lane_cfg *cfg, bool open);

/** @brief Desliga o motor (enable baixo) e desarma o corte pelo feixe. */
int hw_motor_stop(const struct lane_cfg *cfg);

/** @return true enquanto o enable do motor estiver comandado alto. */
bool hw_motor_running(const struct lane_cfg *cfg);

/**
 * @brief Acende ou apaga o vermelho e o verde.
 *
 * Ao apagar o verde, apaga-o antes de mexer no vermelho; ao acender, mexe no
 * vermelho antes. O verde nunca fica aceso sozinho por um instante a mais.
 */
int hw_lights_set(const struct lane_cfg *cfg, bool red, bool green);

/** @brief Gera um pulso de BUZZER_MS no buzzer, sem bloquear. */
int hw_buzzer_pulse(const struct lane_cfg *cfg);

/** @brief Le o feixe de passagem: true quando ha algo no feixe. */
int hw_beam_read(const struct lane_cfg *cfg, bool *active);

/**
 * @brief Retira as bordas do feixe registradas pela ISR.
 * @return Combinacao de HW_BEAM_ROSE, HW_BEAM_FELL e HW_BEAM_LEVEL.
 */
uint32_t hw_beam_take_edges(const struct lane_cfg *cfg);

/** @brief Le os dois fins de curso: true quando acionado. */
int hw_limits_read(const struct lane_cfg *cfg, bool *upper, bool *lower);

#endif /* STUDENT_HW_H_ */
