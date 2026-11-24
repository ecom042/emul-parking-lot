/**
 * @file session.h
 * @brief Tabela de veículos estacionados, compartilhada pelas duas faixas.
 *
 * Fica só na RAM: o reset do laboratório apaga todas as sessões.
 */

#ifndef SESSION_H_
#define SESSION_H_

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Abre a sessão de uma placa que acabou de passar pela entrada.
 *
 * @param plate Placa de 7 caracteres.
 * @param entry_ms Tempo virtual da passagem, início da cobrança.
 * @return 0, -EEXIST se a placa já está estacionada ou -ENOSPC se o
 *         estacionamento está cheio.
 */
int session_add(const char *plate, int64_t entry_ms);

/**
 * @brief Procura a sessão de uma placa.
 *
 * @param plate Placa.
 * @param entry_ms Recebe o tempo da entrada, ou NULL.
 * @return 0 se a placa está estacionada ou -ENOENT.
 */
int session_find(const char *plate, int64_t *entry_ms);

/**
 * @brief Encerra a sessão de uma placa que acabou de passar pela saída.
 *
 * @param plate Placa.
 * @return 0 ou -ENOENT.
 */
int session_remove(const char *plate);

/**
 * @brief Conta os veículos estacionados.
 *
 * @return Número de sessões abertas.
 */
size_t session_count(void);

#endif /* SESSION_H_ */
