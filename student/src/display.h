/*
 * Display ST7789V do terminal de pagamento (REQ-12), desenhado pela SPI
 * real: comandos SWRESET, SLPOUT, COLMOD, MADCTL, DISPON, CASET, RASET e
 * RAMWR. Texto branco sobre preto, fonte 5x7 ampliada 2x.
 *
 * Tela da cobrança:
 *   linha 1: placa           (display_show_plate)
 *   linha 2: BRL r.cc        (display_show_amount)
 *   linha 3: SIMULATED PAYMENT
 *   linha 4: NOT PIX
 */
#ifndef PARKING_DISPLAY_H
#define PARKING_DISPLAY_H

#include <stdint.h>

#define DISPLAY_W 240
#define DISPLAY_H 320

/**
 * Reset por hardware e sequência de inicialização. Bloqueia ~400 ms
 * (esperas do controlador); chamado só no boot, com os motores desligados.
 * @return 0 ou -errno.
 */
int display_init(void);

/** Desenha a placa (linha 1, campo de 7 caracteres). @return 0 ou -errno. */
int display_show_plate(const char *plate);

/**
 * Desenha o valor e os avisos (linhas 2 a 4).
 * @return 0, -EINVAL (valor não formatável) ou -errno da SPI.
 */
int display_show_amount(uint32_t centavos);

/**
 * Desenha uma linha de texto com a fonte, em colunas de 12x16 pixels.
 * Exposta para os testes. @return 0 ou -errno.
 */
int display_text(uint16_t x, uint16_t y, const char *text);

#endif /* PARKING_DISPLAY_H */
