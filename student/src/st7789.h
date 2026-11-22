/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file st7789.h
 * @brief Display do terminal: ST7789V 240x320, RGB565, SPI1.
 *
 * O modulo tem thread propria de prioridade baixa: a inicializacao soma cerca
 * de 400 ms de espera e nao pode atrasar a seguranca das faixas. As chamadas
 * apenas enfileiram o pedido; a ordem na fila e a ordem no barramento SPI.
 */

#ifndef STUDENT_ST7789_H_
#define STUDENT_ST7789_H_

#include <stdint.h>

#define ST7789_WIDTH	240
#define ST7789_HEIGHT	320

/**
 * @brief Cria a thread do display. Nao desenha nada ainda.
 * @retval 0 Sucesso.
 */
int st7789_start(void);

/**
 * @brief Enfileira a tela com a placa reconhecida.
 *
 * Primeiro passo da cobranca: a placa aparece antes do valor e do status.
 *
 * @param plate Placa de 7 caracteres.
 * @retval 0 Enfileirado.
 * @retval -EINVAL Placa invalida.
 * @retval -ENOMSG Fila cheia.
 */
int st7789_show_plate(const char *plate);

/**
 * @brief Enfileira o valor e o status: BRL r.cc, SIMULATED PAYMENT, NOT PIX.
 * @retval 0 Enfileirado.
 * @retval -ENOMSG Fila cheia.
 */
int st7789_show_charge(uint32_t cents);

#endif /* STUDENT_ST7789_H_ */
