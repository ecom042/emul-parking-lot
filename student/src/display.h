/*
 * Display ST7789V do terminal de pagamento (REQ-12).
 *
 * Só desenha: quem chama é a thread do display (main.c), nunca o laço de
 * controle, porque o SPI bloqueia enquanto envia os pixels. As funções
 * usam hw_disp_reset/hw_disp_write/hw_sleep_ms de hw.h.
 *
 * Tela de cobrança (240 x 320, RGB565):
 *
 *   y= 60   ABC1D23            (placa, escala 3)
 *   y=120   BRL 4.00           (valor, escala 3)
 *   y=180   SIMULATED PAYMENT  (escala 2)
 *   y=210   NOT PIX            (escala 2)
 */
#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <stddef.h>
#include <stdint.h>

#define DISPLAY_WIDTH 240
#define DISPLAY_HEIGHT 320

/* Comandos do subconjunto ST7789V usado. */
#define ST7789_SWRESET 0x01
#define ST7789_SLPOUT 0x11
#define ST7789_DISPON 0x29
#define ST7789_CASET 0x2a
#define ST7789_RASET 0x2b
#define ST7789_RAMWR 0x2c
#define ST7789_MADCTL 0x36
#define ST7789_COLMOD 0x3a

#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xffff
#define COLOR_YELLOW 0xffe0

/**
 * Reset por hardware, sequência de inicialização (SWRESET, SLPOUT,
 * COLMOD 55, MADCTL 00, DISPON) e tela preta.
 * @return 0 ou o primeiro erro de SPI/GPIO.
 */
int display_init(void);

/**
 * Primeira etapa da cobrança: desenha a placa e limpa valor e avisos.
 * @return 0 ou erro.
 */
int display_show_plate(const char *plate);

/**
 * Segunda etapa: desenha "BRL r.cc", "SIMULATED PAYMENT" e "NOT PIX".
 * @return 0 ou erro.
 */
int display_show_amount(uint32_t centavos);

/**
 * Formata centavos como "BRL r.cc" (por exemplo 400 -> "BRL 4.00").
 * @return comprimento ou -1 se não couber.
 */
int display_format_amount(char *buf, size_t size, uint32_t centavos);

/**
 * Desenha uma faixa horizontal de texto centralizado, apagando a faixa
 * inteira (largura total, altura 8 x escala).
 * @return 0 ou erro.
 */
int display_text_band(uint16_t y, uint8_t scale, const char *text, uint16_t color);

#endif /* DISPLAY_H_ */
