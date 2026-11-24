/**
 * @file display.h
 * @brief Tela do terminal de pagamento (ST7789V 240x320, RGB565, SPI1).
 *
 * As funções só enfileiram o pedido e retornam na hora: o desenho e os atrasos
 * da inicialização rodam numa thread própria, de prioridade menor que a da
 * máquina de estados, para nunca atrasar o trabalho de segurança das faixas.
 *
 * A tela tem quatro linhas de texto fixas (placa, valor, aviso e "NOT PIX").
 * Cada linha é desenhada numa única janela CASET/RASET/RAMWR, com o texto
 * centralizado e espaços nas sobras, o que apaga o texto anterior sem
 * preencher a tela inteira.
 */

#ifndef DISPLAY_H_
#define DISPLAY_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Largura nativa do painel, em pixels. */
#define DISPLAY_WIDTH 240

/** @brief Altura nativa do painel, em pixels. */
#define DISPLAY_HEIGHT 320

/** @brief Colunas de um caractere da fonte. */
#define DISPLAY_GLYPH_W 5

/** @brief Linhas de um caractere da fonte. */
#define DISPLAY_GLYPH_H 7

/** @brief Largura de uma célula de texto: o caractere e uma coluna de espaço. */
#define DISPLAY_CELL_W(scale) ((DISPLAY_GLYPH_W + 1) * (scale))

/** @brief Altura de uma célula de texto: o caractere e uma linha de espaço. */
#define DISPLAY_CELL_H(scale) ((DISPLAY_GLYPH_H + 1) * (scale))

/** @brief Tamanho da placa, sem o NUL. */
#define DISPLAY_PLATE_LEN 7

/** @brief Maior valor que a tela mostra, em centavos (o limite do terminal). */
#define DISPLAY_MAX_AMOUNT 1000000U

/** @brief Pedidos que a thread do display executa. */
enum display_op {
	DISPLAY_OP_INIT = 0, /**< Reset por hardware e sequência de inicialização. */
	DISPLAY_OP_PLATE,    /**< Placa na primeira linha; as outras apagadas. */
	DISPLAY_OP_CHARGE,   /**< Valor, "SIMULATED PAYMENT" e "NOT PIX"; a placa fica. */
	DISPLAY_OP_CLEAR,    /**< Apaga as quatro linhas. */
};

/** @brief Um pedido na fila do display. */
struct display_request {
	enum display_op op;                 /**< O que fazer. */
	uint32_t amount;                    /**< Valor em centavos, em DISPLAY_OP_CHARGE. */
	char plate[DISPLAY_PLATE_LEN + 1];  /**< Placa, em DISPLAY_OP_PLATE. */
};

/** @brief Posição, tamanho e cor de uma linha de texto da tela. */
struct display_line {
	uint16_t x;     /**< Coluna do canto superior esquerdo. */
	uint16_t y;     /**< Linha do canto superior esquerdo. */
	uint8_t scale;  /**< Fator de ampliação da fonte (1 pixel vira scale x scale). */
	uint8_t cells;  /**< Caracteres que cabem na linha. */
	uint16_t fg;    /**< Cor do texto, RGB565. */
};

/**
 * @brief Inicia a thread do display, que faz o reset por hardware e a
 *        sequência SWRESET, SLPOUT, COLMOD 55, MADCTL 00, DISPON.
 *
 * @return 0 em sucesso ou -EALREADY se a thread já existe.
 */
int display_init(void);

/**
 * @brief Desenha a placa reconhecida e apaga as outras linhas.
 *
 * @param plate Placa de 7 caracteres, só A-Z e 0-9.
 * @return 0 se o pedido foi enfileirado, -EINVAL para placa inválida,
 *         -ENOMEM com a fila cheia ou -EIO se o display falhou antes.
 */
int display_show_plate(const char *plate);

/**
 * @brief Desenha "BRL <reais>.<centavos>", "SIMULATED PAYMENT" e "NOT PIX".
 *
 * @param amount Valor em centavos, até DISPLAY_MAX_AMOUNT.
 * @return 0 se o pedido foi enfileirado, -EINVAL acima do limite, -ENOMEM
 *         com a fila cheia ou -EIO se o display falhou antes.
 */
int display_show_charge(uint32_t amount);

/**
 * @brief Consulta a conclusão dos pedidos de desenho, sem esperar.
 *
 * @return 0 quando todos terminaram, -EAGAIN com pedidos em execução ou na
 *         fila, ou -EIO se houve falha no painel.
 */
int display_status(void);

/**
 * @brief Apaga a tela.
 *
 * @return 0 se o pedido foi enfileirado, -ENOMEM com a fila cheia ou -EIO se
 *         o display falhou antes.
 */
int display_clear(void);

/**
 * @brief Executa um pedido na hora, na thread de quem chama.
 *
 * É o que a thread do display faz com cada pedido da fila. Pode bloquear
 * (k_msleep) durante a inicialização: nunca chame da thread main.
 *
 * @param req Pedido.
 * @return 0 em sucesso, -EINVAL para pedido inválido ou o erro de hw_display_*.
 */
int display_handle(const struct display_request *req);

/**
 * @brief Formata o valor como "BRL <reais>.<centavos com 2 dígitos>".
 *
 * Por exemplo, 400 vira "BRL 4.00" e 1234 vira "BRL 12.34".
 *
 * @param amount Valor em centavos, até DISPLAY_MAX_AMOUNT.
 * @param buf Destino.
 * @param size Tamanho do destino.
 * @return 0 em sucesso, -EINVAL acima do limite ou -ENOSPC se não coube.
 */
int display_format_amount(uint32_t amount, char *buf, size_t size);

/**
 * @brief Gera uma linha de pixels de uma linha de texto, em RGB565 com o byte
 *        alto primeiro.
 *
 * O texto é centralizado nas células da linha e o resto é preenchido com o
 * fundo.
 *
 * @param line Linha de texto da tela.
 * @param text Texto, só com caracteres da fonte (A-Z, 0-9, '.', ' ').
 * @param py Linha de pixels, de 0 a DISPLAY_CELL_H(line->scale) - 1.
 * @param buf Destino dos bytes.
 * @param size Tamanho do destino.
 * @return Bytes escritos, -EINVAL para escala, texto ou py inválidos ou
 *         -ENOSPC se o destino é pequeno.
 */
int display_render_row(const struct display_line *line, const char *text, unsigned int py,
		       uint8_t *buf, size_t size);

#endif /* DISPLAY_H_ */
