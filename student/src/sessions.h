/*
 * Tabela de sessões (REQ-04): quais placas estão estacionadas e desde quando.
 * Compartilhada pelas duas faixas. Não é persistente: o reset a apaga.
 */
#ifndef SESSIONS_H_
#define SESSIONS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Apaga todas as sessões (partida e testes). */
void sessions_reset(void);

/** Número de sessões ativas. */
size_t sessions_count(void);

/** true se ainda há vaga (sessões < CONFIG_PARKING_CAPACITY). */
bool sessions_has_space(void);

/** true se a placa tem sessão ativa. */
bool sessions_contains(const char *plate);

/**
 * Cria a sessão da placa no instante now_ms (passagem completa na entrada).
 * @return 0; -EEXIST (já estacionada); -ENOSPC (lotado).
 */
int sessions_add(const char *plate, int64_t now_ms);

/**
 * Lê o instante de entrada da placa.
 * @return 0; -ENOENT (sem sessão).
 */
int sessions_entry_time(const char *plate, int64_t *entry_ms);

/**
 * Encerra a sessão da placa (passagem completa na saída) e libera a vaga.
 * @return 0; -ENOENT (sem sessão).
 */
int sessions_remove(const char *plate);

#endif /* SESSIONS_H_ */
