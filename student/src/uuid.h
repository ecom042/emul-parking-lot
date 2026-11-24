/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file uuid.h
 * @brief Identificador de cobranca no formato 8-4-4-4-12.
 */

#ifndef STUDENT_UUID_H_
#define STUDENT_UUID_H_

/** Tamanho do UUID em texto, sem o terminador. */
#define UUID_LEN 36

/**
 * @brief Gera um UUID novo em hexadecimal minusculo.
 *
 * Mistura entropia, um contador de processo e o tempo virtual: em Renode a
 * fonte de entropia pode ser deterministica e dois UUIDs iguais quebrariam a
 * regra de reenvio idempotente.
 *
 * @param out Buffer de 37 bytes (36 caracteres e o terminador).
 */
void uuid_generate(char out[UUID_LEN + 1]);

#endif /* STUDENT_UUID_H_ */
