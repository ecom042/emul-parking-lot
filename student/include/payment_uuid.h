/*
 * payment_uuid.h — Identificador (UUID) de cada tentativa de saída
 * ================================================================
 *
 * PARA QUE SERVE?
 *   Cada vez que um carro tenta sair, o firmware cria UM UUID novo. Ele
 *   identifica aquela cobrança no terminal: a resposta "PAID" só vale se trouxer
 *   exatamente esse UUID. É o que impede que um PAID antigo (de outro carro ou
 *   de uma tentativa passada) abra a cancela — o cenário payment-stale.
 *
 * FORMATO (contrato): 36 caracteres hexadecimais MINÚSCULOS no padrão 8-4-4-4-12,
 *   ex.: 3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1
 *
 * ESTRATÉGIA ESCOLHIDA (o desafio manda escolher e explicar):
 *   - Não há gerador de números aleatórios garantido na placa simulada, e o
 *     tempo é virtual. Então usamos um CONTADOR que só cresce.
 *   - Os últimos 12 dígitos hexadecimais SÃO o contador => dois UUIDs de
 *     contadores diferentes são, por construção, diferentes (unicidade garantida).
 *   - Os demais dígitos vêm de um embaralhador (splitmix64) do contador, só para
 *     o UUID parecer um UUID versão 4 (nibble de versão '4', variante 8..b).
 *   LIMITAÇÃO conhecida: depois de um reset da máquina o contador recomeça. O
 *   simulador também zera o terminal no reset, então não há colisão aqui; um
 *   produto real precisaria de um contador durável.
 */
#ifndef PARKING_PAYMENT_UUID_H
#define PARKING_PAYMENT_UUID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAYMENT_UUID_LEN 36U /* tamanho como size_t abaixo */
#define PAYMENT_UUID_SIZE (PAYMENT_UUID_LEN + 1U) /* + '\0' */

/* Gera o UUID do contador `counter` em out (36 caracteres + '\0'). */
void payment_uuid_make(uint64_t counter, char out[PAYMENT_UUID_SIZE]);

/* true se `text` TEM EXATAMENTE 36 caracteres no formato 8-4-4-4-12 minúsculo. */
bool payment_uuid_valid(const char *text);

/* true se os PRIMEIROS 36 caracteres de `text` têm o formato (o resto é ignorado). */
bool payment_uuid_prefix_valid(const char *text);

#endif /* PARKING_PAYMENT_UUID_H */
