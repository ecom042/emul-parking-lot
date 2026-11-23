/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file hw.h
 * @brief Camada fina de GPIO de uma faixa: motor, sinais, buzzer, feixe e
 *        fins de curso.
 *
 * Contrato congelado da dupla (fonte de verdade, secao 2). Implementado em
 * hw_io.c. So os arquivos hw_*.c e st7789.c incluem headers de driver; a
 * logica usa apenas estas funcoes, para poder ser testada com FFF.
 *
 * A UART e o display nao entram aqui: use hw_uart_send(), hw_uart_poll_plate()
 * e hw_uart_poll_payment() (hw_uart.h) e st7789_show_plate() e
 * st7789_show_charge() (st7789.h).
 *
 * Todas as funcoes devolvem 0 em sucesso ou um errno negativo.
 */

#ifndef STUDENT_HW_H_
#define STUDENT_HW_H_

#include <stdbool.h>

/**
 * @brief Configuracao de uma faixa: as duas instancias ficam em lane_cfg.c.
 *
 * Pelo roadmap B ela reune se a faixa tem pagamento, os aliases do
 * devicetree e o canal do ADC4. Os campos ainda nao foram definidos.
 */
struct lane_cfg;

/**
 * @brief Liga o motor no sentido pedido.
 * @param open true abre (direcao alta), false fecha (direcao baixa).
 */
int hw_motor_drive(const struct lane_cfg *cfg, bool open);

/** @brief Desliga o motor (enable baixo). */
int hw_motor_stop(const struct lane_cfg *cfg);

/** @brief Acende ou apaga o vermelho e o verde. */
int hw_lights_set(const struct lane_cfg *cfg, bool red, bool green);

/** @brief Gera um pulso de BUZZER_MS no buzzer, sem bloquear. */
int hw_buzzer_pulse(const struct lane_cfg *cfg);

/** @brief Le o feixe de passagem: true quando ha algo no feixe. */
int hw_beam_read(const struct lane_cfg *cfg, bool *active);

/** @brief Le os dois fins de curso: true quando acionado. */
int hw_limits_read(const struct lane_cfg *cfg, bool *upper, bool *lower);

#endif /* STUDENT_HW_H_ */
