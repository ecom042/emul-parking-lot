/*
 * display.h — Display do terminal (ST7789V por SPI): REQ-12
 * =========================================================
 *
 * PARA QUE SERVE?
 *   Desenha no display do terminal de pagamento as duas telas do REQ-12 e
 *   cumpre o contrato `struct payment_display_ops` (payment.h) que o módulo de
 *   pagamento (issue #6) espera. O cenário lê o que o MODELO recebeu pela SPI;
 *   texto no console não conta.
 *
 * COMO O TRABALHO É DIVIDIDO (tudo SEM BLOQUEAR a segurança):
 *   Falar com o ST7789V é lento e tem esperas (a inicialização leva ~400 ms).
 *   Se o firmware ficasse parado esperando, a OUTRA faixa (cancela, obstrução)
 *   ficaria sem atenção. Então tudo é uma máquina de estados "em fatias":
 *   display_service(now) faz um PEDAÇO pequeno do trabalho e volta.
 *
 *   1) INICIALIZAÇÃO (começa em display_init(), avança em display_service()):
 *        reset por hardware (segura 10 ms, solta, espera 120 ms)
 *        SWRESET (espera 150 ms) -> SLPOUT (espera 120 ms)
 *        COLMOD 0x55 (RGB565) -> MADCTL 0x00 -> DISPON
 *   2) DESENHO: cada tela é uma lista de retângulos de texto. Cada chamada de
 *      display_service() envia no máximo DISPLAY_ROWS_PER_STEP linhas de pixels.
 *
 * COMO O CONTROLE USA (integração, issue #8):
 *   - no boot:               display_init();
 *   - a cada passo do laço:  display_service(now);   <- faz a inicialização andar
 *   - ao criar o pagamento:  payment_init(display_ops());
 */
#ifndef PARKING_DISPLAY_H
#define PARKING_DISPLAY_H

#include <stdint.h>

#include "equipment_types.h"

struct payment_display_ops;

/* Máximo de linhas de pixels enviadas por chamada de display_service(). */
#define DISPLAY_ROWS_PER_STEP 4U

/* Confere SPI/GPIOs e começa a inicialização. 0 ou -errno. */
int display_init(void);

/* Avança a inicialização e/ou o desenho em andamento. Não bloqueia. `now` em ms virtuais. */
void display_service(int64_t now);

/* ---- funções da tabela struct payment_display_ops ---- */

/* Pede o desenho da tela da PLACA. 0 = aceito; -ENODEV sem display_init(); -EIO se a
 * inicialização falhou; -EBUSY se há desenho em andamento; -EINVAL argumento inválido. */
int display_draw_plate(const char plate[PARKING_PLATE_SIZE]);

/* Pede o desenho da tela da COBRANÇA (placa, BRL r.cc, SIMULATED PAYMENT, NOT PIX). */
int display_draw_charge(const char plate[PARKING_PLATE_SIZE], uint32_t amount_centavos);

/* Andamento do último pedido: PENDING, OK ou FAILED (também FAILED se nada foi pedido). */
enum equipment_result display_poll(int64_t now);

/* Abandona o desenho em andamento. */
void display_cancel(void);

/* A tabela pronta para payment_init(). */
const struct payment_display_ops *display_ops(void);

#ifdef CONFIG_ZTEST
void display_reset_for_test(void);
#endif

#endif /* PARKING_DISPLAY_H */
