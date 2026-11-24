/*
 * Tabela de sessões (REQ-04): uma por carro estacionado, chaveada pela
 * placa, com até CONFIG_PARKING_CAPACITY entradas. Compartilhada pelas duas
 * faixas; só o laço principal a usa.
 */
#ifndef PARKING_SESSIONS_H
#define PARKING_SESSIONS_H

#include <stdbool.h>
#include <stdint.h>

#include "camera.h"

/** Esvazia a tabela (boot/reset). */
void sessions_clear(void);

/** Quantas sessões ativas. */
int sessions_count(void);

/** true se ainda há vaga. */
bool sessions_has_space(void);

/**
 * Procura a sessão da placa.
 * @return true e *entry_ms (instante da passagem de entrada) se existir.
 */
bool sessions_find(const char *plate, int64_t *entry_ms);

/** Cria a sessão. @return 0, -EEXIST (já estacionada) ou -ENOSPC (lotado). */
int sessions_add(const char *plate, int64_t entry_ms);

/** Encerra a sessão. @return 0 ou -ENOENT. */
int sessions_remove(const char *plate);

#endif /* PARKING_SESSIONS_H */
