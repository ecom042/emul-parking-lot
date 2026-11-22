/*
 * camera_hw.h — Camada FINA de hardware das câmeras (UART)
 * ========================================================
 *
 * PARA QUE SERVE?
 *   É a única fronteira entre a lógica da câmera (camera.c) e o driver UART
 *   real do Zephyr. Só três operações:
 *
 *       camera_hw_init()  -> confere se as duas UARTs existem e estão prontas;
 *       camera_hw_send()  -> envia bytes para a câmera de uma faixa;
 *       camera_hw_read()  -> pega os bytes que já chegaram (sem esperar).
 *
 * POR QUE EXISTE (em vez de chamar uart_poll_out() dentro de camera.c)?
 *   Para poder testar a lógica SEM hardware. Nos testes de unidade, estas três
 *   funções são trocadas por falsos do FFF: o teste escolhe quais bytes
 *   "chegam" e inclusive força erros (-EIO), que os cenários do Renode nunca
 *   provocam. Os caminhos de erro só se cobrem assim (veja o desafio,
 *   "Testes de unidade").
 *
 * ÍNDICE DA FAIXA:
 *   `lane` é o mesmo número de enum parking_lane_id: 0 = entrada (USART3),
 *   1 = saída (USART2).
 *
 * VALORES DE RETORNO (convenção do Zephyr: 0/positivo = ok, negativo = -errno):
 *   -EINVAL faixa inexistente ou ponteiro nulo; -ENODEV UART não está pronta;
 *   -EIO erro do driver durante a leitura.
 */
#ifndef PARKING_CAMERA_HW_H
#define PARKING_CAMERA_HW_H

#include <stddef.h>
#include <stdint.h>

/* Verifica as UARTs das duas câmeras. 0 se tudo certo, -ENODEV se alguma não estiver pronta. */
int camera_hw_init(void);

/* Envia `len` bytes à câmera da faixa. Devolve 0 ou -errno. Retorna rápido: são ~16 bytes. */
int camera_hw_send(unsigned int lane, const uint8_t *data, size_t len);

/*
 * Lê até `max` bytes que JÁ chegaram da câmera, sem bloquear.
 * Devolve quantos bytes leu (0 = nada novo) ou -errno.
 */
int camera_hw_read(unsigned int lane, uint8_t *buf, size_t max);

#endif /* PARKING_CAMERA_HW_H */
