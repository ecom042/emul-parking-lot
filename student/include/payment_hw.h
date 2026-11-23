/*
 * payment_hw.h — Camada FINA de hardware do terminal de pagamento (LPUART1)
 * =========================================================================
 * Mesma ideia de camera_hw.h: a ÚNICA fronteira entre a lógica (payment.c) e o
 * driver UART real. Assim os testes trocam estas três funções por falsos FFF e
 * conseguem forçar erros de driver (-EIO). Só existe UM terminal (na saída).
 *
 *   payment_hw_init() -> confere se a UART do terminal está pronta (-ENODEV se não);
 *   payment_hw_send() -> envia bytes ao terminal (0 ou -errno);
 *   payment_hw_read() -> lê os bytes que JÁ chegaram, sem bloquear
 *                        (devolve quantos leu, 0 = nada novo, ou -errno).
 */
#ifndef PARKING_PAYMENT_HW_H
#define PARKING_PAYMENT_HW_H

#include <stddef.h>
#include <stdint.h>

int payment_hw_init(void);
int payment_hw_send(const uint8_t *data, size_t len);
int payment_hw_read(uint8_t *buf, size_t max);

#endif /* PARKING_PAYMENT_HW_H */
