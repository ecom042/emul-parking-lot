/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file hw_uart.h
 * @brief UARTs dos equipamentos: RX por interrupcao, TX sem bloquear.
 *
 * As tres UARTs (camera da entrada, camera da saida, terminal) tem buffer de
 * recepcao proprio e analisador proprio. Esta camada nao conhece a maquina de
 * estados: ela entrega quadros ja validados e recebe linhas para enviar.
 */

#ifndef STUDENT_HW_UART_H_
#define STUDENT_HW_UART_H_

#include <stdbool.h>

#include "at_parser.h"
#include "p1_parser.h"

/** Portas dos equipamentos. */
enum hw_uart_port {
	HW_UART_ENTRY_CAMERA,
	HW_UART_EXIT_CAMERA,
	HW_UART_PAYMENT,
	HW_UART_PORT_COUNT,
};

/**
 * @brief Liga as tres UARTs: buffers, analisadores e interrupcoes de recepcao.
 * @retval 0 Sucesso.
 * @retval -ENODEV Algum dispositivo do devicetree nao esta pronto.
 */
int hw_uart_init(void);

/**
 * @brief Envia uma linha, com o terminador da porta.
 *
 * Acrescenta CR LF nas cameras e LF no terminal. Nao bloqueia: copia para um
 * buffer estatico e deixa a interrupcao de transmissao esvaziar o FIFO.
 *
 * @param port Porta de destino.
 * @param line Linha sem terminador (por exemplo, "AT+CAPTURE=17").
 * @retval 0 Sucesso.
 * @retval -EINVAL Porta ou linha invalida.
 * @retval -EBUSY Envio anterior ainda em andamento.
 * @retval -EMSGSIZE Linha maior que o quadro maximo.
 */
int hw_uart_send(enum hw_uart_port port, const char *line);

/**
 * @brief Retira um +PLATE valido da camera.
 * @retval true Um quadro foi entregue em @p out.
 */
bool hw_uart_poll_plate(enum hw_uart_port port, struct at_plate *out);

/**
 * @brief Retira um P1 RESULT ou P1 ERROR valido do terminal.
 * @retval true Um quadro foi entregue em @p out.
 */
bool hw_uart_poll_payment(struct p1_result *out);

#endif /* STUDENT_HW_UART_H_ */
