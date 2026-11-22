/*
 * display_render.h — Layout e renderização de texto em pixels RGB565
 * ===================================================================
 *
 * PARA QUE SERVE?
 *   Responde, sem tocar em hardware nenhum, a duas perguntas:
 *     1) O QUE aparece em cada tela e ONDE?        -> layout (display_layout_*)
 *     2) QUAIS BYTES formam cada linha de pixels?  -> display_render_row()
 *   Como é lógica pura, os testes conferem posições, cores e valores sem SPI.
 *
 * AS DUAS TELAS (REQ-12):
 *   - tela da PLACA:   a placa reconhecida (e apaga o valor da saída anterior).
 *   - tela da COBRANÇA: placa, "BRL r.cc", "SIMULATED PAYMENT" e "NOT PIX".
 *
 * CONCEITOS:
 *   - "item" = UMA linha de texto = UM retângulo da tela. Cada item é pintado
 *     com uma única janela do ST7789V (CASET/RASET/RAMWR).
 *   - O texto é desenhado com fundo opaco (bg). Assim, desenhar de novo sobre
 *     um retângulo apaga o que havia antes (valor da saída anterior, p.ex.).
 *   - "scale" amplia a fonte 5x7: cada pixel da letra vira um quadrado
 *     scale x scale. Cada letra ocupa 6 colunas (5 + 1 de espaço) x scale.
 *   - Cor RGB565 = 16 bits (5 vermelho, 6 verde, 5 azul), enviada com o byte
 *     ALTO primeiro (contrato do modelo).
 *
 * POR QUE NÃO PINTAR A TELA INTEIRA?
 *   240x320 pixels = 153.600 bytes por SPI, e isso gasta tempo virtual do
 *   firmware antes de mandar a cobrança ao terminal (prazo apertado no cenário
 *   payment-delayed). Pintamos só os retângulos de texto (~15% da tela).
 */
#ifndef PARKING_DISPLAY_RENDER_H
#define PARKING_DISPLAY_RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "font5x7.h"

#define DISPLAY_WIDTH 240U
#define DISPLAY_HEIGHT 320U
#define DISPLAY_CELL_COLS 6U /* 5 colunas da letra + 1 de espaço */
#define DISPLAY_ROW_BYTES_MAX (DISPLAY_WIDTH * 2U) /* uma linha cheia, 2 bytes por pixel */

#define DISPLAY_PLATE_CHARS 7U
#define DISPLAY_AMOUNT_CHARS 12U /* "BRL 10000.00": o maior valor possível (1.000.000 centavos) */
#define DISPLAY_TEXT_MAX 24U     /* maior texto + '\0' ("SIMULATED PAYMENT" tem 17) */
#define DISPLAY_MAX_ITEMS 4U

/* Cores RGB565 usadas. */
#define DISPLAY_COLOR_BLACK 0x0000U
#define DISPLAY_COLOR_WHITE 0xFFFFU
#define DISPLAY_COLOR_YELLOW 0xFFE0U
#define DISPLAY_COLOR_GREEN 0x07E0U
#define DISPLAY_COLOR_RED 0xF800U

/* Uma linha de texto a pintar: um retângulo da tela. */
struct display_item {
	char text[DISPLAY_TEXT_MAX]; /* texto terminado em '\0' */
	uint16_t x;                  /* canto superior esquerdo, em pixels */
	uint16_t y;
	uint8_t chars;               /* quantos caracteres (largura = chars * 6 * scale) */
	uint8_t scale;               /* ampliação da fonte (1, 2, 3...) */
	uint16_t fg;                 /* cor da letra */
	uint16_t bg;                 /* cor do fundo */
};

/* Largura e altura do retângulo do item, em pixels. */
uint16_t display_item_width(const struct display_item *item);
uint16_t display_item_height(const struct display_item *item);

/*
 * Gera a linha de pixels `row` (0 = a de cima) do item em out, no formato
 * RGB565 com byte alto primeiro. Devolve quantos bytes escreveu
 * (= largura * 2, no máximo DISPLAY_ROW_BYTES_MAX). `row` fora do item => 0.
 */
size_t display_render_row(const struct display_item *item, unsigned int row, uint8_t *out);

/*
 * Formata o valor como "BRL <reais>.<centavos com 2 dígitos>", completado com
 * espaços à direita até DISPLAY_AMOUNT_CHARS (largura fixa: apaga o valor
 * anterior por inteiro). Ex.: 400 -> "BRL 4.00". false se não couber
 * (acima de 1.000.000 centavos, que é o máximo do terminal).
 */
bool display_amount_text(uint32_t centavos, char out[DISPLAY_AMOUNT_CHARS + 1U]);

/* Tela da PLACA: devolve quantos itens preencheu em items (0 se `plate` for NULL). */
size_t display_layout_plate(const char *plate, struct display_item items[DISPLAY_MAX_ITEMS]);

/* Tela da COBRANÇA: 4 itens. Devolve 0 se `plate` for NULL ou o valor não couber. */
size_t display_layout_charge(const char *plate, uint32_t centavos,
			     struct display_item items[DISPLAY_MAX_ITEMS]);

#endif /* PARKING_DISPLAY_RENDER_H */
