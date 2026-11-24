/*
 * Pedidos de desenho do laço de controle para a thread do display.
 *
 * O laço de controle não pode esperar o SPI (a outra faixa pode precisar
 * parar um motor em 10 ms). Então ele só PEDE o desenho e segue; quando a
 * thread do display termina, o laço recebe lane_on_display_done().
 * Implementado em main.c (fila k_msgq); nos testes, por falsos do FFF.
 */
#ifndef UI_H_
#define UI_H_

#include <stdint.h>

/** Pede a primeira etapa da tela de cobrança (placa). */
int ui_request_plate(const char *plate);

/** Pede a segunda etapa (valor, SIMULATED PAYMENT, NOT PIX). */
int ui_request_amount(uint32_t centavos);

#endif /* UI_H_ */
