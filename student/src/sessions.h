/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file sessions.h
 * @brief Tabela de sessoes ativas, uma por placa.
 *
 * A sessao nasce na passagem completa da entrada e morre na passagem completa
 * da saida. Pagamento ou abertura sozinhos nunca mexem na tabela.
 */

#ifndef STUDENT_SESSIONS_H_
#define STUDENT_SESSIONS_H_

#include <stdbool.h>
#include <stdint.h>

/** Esvazia a tabela. Um reset tambem faz isso. */
void sessions_reset(void);

/** @return true quando nao ha vaga livre. */
bool sessions_full(void);

/** @return Quantidade de sessoes ativas. */
uint8_t sessions_count(void);

/** @return true quando a placa tem sessao ativa. */
bool sessions_has(const char *plate);

/**
 * @brief Cria a sessao de uma placa.
 * @retval 0 Sucesso.
 * @retval -EINVAL Placa invalida.
 * @retval -EEXIST A placa ja tem sessao.
 * @retval -ENOSPC Nao ha vaga livre.
 */
int sessions_add(const char *plate, int64_t entry_ms);

/**
 * @brief Remove a sessao de uma placa.
 * @retval 0 Sucesso.
 * @retval -EINVAL Placa invalida.
 * @retval -ENOENT A placa nao tem sessao.
 */
int sessions_remove(const char *plate);

/**
 * @brief Le o instante de entrada de uma sessao.
 * @retval 0 Sucesso.
 * @retval -EINVAL Argumento invalido.
 * @retval -ENOENT A placa nao tem sessao.
 */
int sessions_entry_time(const char *plate, int64_t *entry_ms);

#endif /* STUDENT_SESSIONS_H_ */
