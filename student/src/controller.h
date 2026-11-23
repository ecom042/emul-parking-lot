/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file controller.h
 * @brief Thread de controle unica: transforma o hardware em eventos para as
 *        duas faixas.
 *
 * A cada tick: um servico do ADC4 (dono unico), depois, para cada faixa,
 * fins de curso, bordas do feixe, quadros da camera e do terminal e o tick
 * dos prazos. Nada aqui bloqueia; a seguranca das duas faixas e servida no
 * mesmo laco, entao a espera de uma nao atrasa a outra.
 */

#ifndef STUDENT_CONTROLLER_H_
#define STUDENT_CONTROLLER_H_

#include <stdint.h>

#include "lane.h"

/**
 * @brief Liga o hardware e prepara as duas faixas para a partida segura.
 * @retval 0 Sucesso (uma falha do ADC4 trava as faixas, mas nao impede).
 * @retval <0 GPIO ou UART indisponivel: o controlador nao pode rodar.
 */
int controller_init(void);

/** @brief Um tick de servico, no tempo virtual @p now_ms. */
void controller_step(int64_t now_ms);

/**
 * @brief Inicializa e roda a thread de controle para sempre.
 *
 * Chama controller_init(); se falhar, imprime PARKING_STUDENT_SETUP_FAILED e
 * devolve o erro. Senao imprime PARKING_STUDENT_READY e chama
 * controller_step() a cada TICK_MS, acordando em instantes absolutos de tempo
 * virtual (o periodo nao escorrega com o tempo de servico).
 *
 * @retval <0 Erro de controller_init(); em sucesso a funcao nao retorna.
 */
int controller_run(void);

/** @brief Estado de uma faixa, para testes e diagnostico. */
const struct lane *controller_lane(enum lane_id id);

#endif /* STUDENT_CONTROLLER_H_ */
