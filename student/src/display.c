/**
 * @file display.c
 * @brief Tela do terminal de pagamento (ST7789V 240x320, RGB565, SPI1).
 *
 * Três camadas: as funções públicas validam e enfileiram; a thread do display
 * tira cada pedido da fila e chama display_handle(); display_handle() fala com
 * o painel por hw_display_*(). Formatar o valor e gerar os pixels são funções
 * puras, testadas sem thread nem hardware.
 */

#include "display.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "hw.h"

/** @brief Prioridade da thread do display: preemptiva e abaixo da main. */
#define DISPLAY_THREAD_PRIORITY K_PRIO_PREEMPT(5)
BUILD_ASSERT(DISPLAY_THREAD_PRIORITY > CONFIG_MAIN_THREAD_PRIORITY,
	     "o display precisa de prioridade menor (número maior) que a main");

#define DISPLAY_STACK_SIZE 1536
#define DISPLAY_QUEUE_LEN  8

/** @brief Comandos ST7789V usados. */
#define ST7789_SWRESET 0x01
#define ST7789_SLPOUT  0x11
#define ST7789_DISPON  0x29
#define ST7789_CASET   0x2a
#define ST7789_RASET   0x2b
#define ST7789_RAMWR   0x2c
#define ST7789_MADCTL  0x36
#define ST7789_COLMOD  0x3a

/** @brief Parâmetros da inicialização: RGB565 e orientação nativa. */
#define ST7789_COLMOD_RGB565 0x55
#define ST7789_MADCTL_NATIVE 0x00

/** @brief Atrasos do reset por hardware, em ms. */
#define RESET_PULSE_MS 10
#define RESET_WAIT_MS  120

/** @brief Cores RGB565. */
#define COLOR_BLACK  0x0000
#define COLOR_WHITE  0xffff
#define COLOR_GREEN  0x07e0
#define COLOR_YELLOW 0xffe0
#define COLOR_BG     COLOR_BLACK

/** @brief Escala e células das linhas grandes (placa e valor) e pequenas (avisos). */
#define BIG_SCALE   3
#define BIG_CELLS   12
#define SMALL_SCALE 2
#define SMALL_CELLS 17

/** @brief Topo de cada linha de texto, em pixels. */
#define PLATE_Y  60
#define AMOUNT_Y 110
#define STATUS_Y 170
#define NOTICE_Y 200

/** @brief Coluna que centraliza uma linha de n células na escala dada. */
#define CENTER_X(scale, n) ((DISPLAY_WIDTH - (n) * DISPLAY_CELL_W(scale)) / 2)

/** @brief Bytes de uma linha de pixels da linha de texto mais larga. */
#define ROW_BYTES (DISPLAY_WIDTH * 2)

BUILD_ASSERT(BIG_CELLS * DISPLAY_CELL_W(BIG_SCALE) <= DISPLAY_WIDTH, "linha grande não cabe");
BUILD_ASSERT(SMALL_CELLS * DISPLAY_CELL_W(SMALL_SCALE) <= DISPLAY_WIDTH, "linha pequena não cabe");
BUILD_ASSERT(NOTICE_Y + DISPLAY_CELL_H(SMALL_SCALE) <= DISPLAY_HEIGHT, "última linha não cabe");
BUILD_ASSERT(sizeof("BRL 10000.00") - 1 <= BIG_CELLS, "o maior valor não cabe na linha");
BUILD_ASSERT(sizeof("SIMULATED PAYMENT") - 1 <= SMALL_CELLS, "o aviso não cabe na linha");

/** @brief Linhas de texto da tela, na ordem de cima para baixo. */
enum display_slot {
	SLOT_PLATE = 0, /**< Placa. */
	SLOT_AMOUNT,    /**< "BRL r.cc". */
	SLOT_STATUS,    /**< "SIMULATED PAYMENT". */
	SLOT_NOTICE,    /**< "NOT PIX". */
	SLOT_COUNT,     /**< Quantidade de linhas. */
};

static const struct display_line slots[SLOT_COUNT] = {
	[SLOT_PLATE] = {CENTER_X(BIG_SCALE, BIG_CELLS), PLATE_Y, BIG_SCALE, BIG_CELLS, COLOR_WHITE},
	[SLOT_AMOUNT] = {CENTER_X(BIG_SCALE, BIG_CELLS), AMOUNT_Y, BIG_SCALE, BIG_CELLS, COLOR_GREEN},
	[SLOT_STATUS] = {CENTER_X(SMALL_SCALE, SMALL_CELLS), STATUS_Y, SMALL_SCALE, SMALL_CELLS,
			 COLOR_WHITE},
	[SLOT_NOTICE] = {CENTER_X(SMALL_SCALE, SMALL_CELLS), NOTICE_Y, SMALL_SCALE, SMALL_CELLS,
			 COLOR_YELLOW},
};

/** @brief Um passo da inicialização: comando, parâmetro opcional e espera depois. */
struct init_step {
	uint8_t cmd;       /**< Comando ST7789V. */
	uint8_t param;     /**< Parâmetro, se len == 1. */
	uint8_t len;       /**< 0 ou 1 parâmetro. */
	uint16_t delay_ms; /**< Espera depois do comando. */
};

static const struct init_step init_steps[] = {
	{ST7789_SWRESET, 0, 0, 150},
	{ST7789_SLPOUT, 0, 0, 120},
	{ST7789_COLMOD, ST7789_COLMOD_RGB565, 1, 0},
	{ST7789_MADCTL, ST7789_MADCTL_NATIVE, 1, 0},
	{ST7789_DISPON, 0, 0, 0},
};

/*
 * Fonte 5x7 própria, desenhada para este projeto. Cada byte é uma linha do
 * caractere, de cima para baixo; o bit 4 é a coluna da esquerda.
 */
static const uint8_t font_space[DISPLAY_GLYPH_H] = {0};
static const uint8_t font_dot[DISPLAY_GLYPH_H] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c};
static const uint8_t font_digits[10][DISPLAY_GLYPH_H] = {
	{0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}, /* 0 */
	{0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}, /* 1 */
	{0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}, /* 2 */
	{0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e}, /* 3 */
	{0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}, /* 4 */
	{0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e}, /* 5 */
	{0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}, /* 6 */
	{0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, /* 7 */
	{0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}, /* 8 */
	{0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c}, /* 9 */
};
static const uint8_t font_letters[26][DISPLAY_GLYPH_H] = {
	{0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}, /* A */
	{0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}, /* B */
	{0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}, /* C */
	{0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}, /* D */
	{0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}, /* E */
	{0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}, /* F */
	{0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f}, /* G */
	{0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}, /* H */
	{0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}, /* I */
	{0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c}, /* J */
	{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, /* K */
	{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}, /* L */
	{0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}, /* M */
	{0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}, /* N */
	{0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}, /* O */
	{0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}, /* P */
	{0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}, /* Q */
	{0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}, /* R */
	{0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}, /* S */
	{0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, /* T */
	{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}, /* U */
	{0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}, /* V */
	{0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}, /* W */
	{0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}, /* X */
	{0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04}, /* Y */
	{0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}, /* Z */
};

/** @brief Caracteres permitidos numa placa. */
static const char plate_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

K_MSGQ_DEFINE(display_queue, sizeof(struct display_request), DISPLAY_QUEUE_LEN, 4);
K_THREAD_STACK_DEFINE(display_stack, DISPLAY_STACK_SIZE);
static struct k_thread display_thread_data;
static k_tid_t display_tid;

/** @brief Bit 0 aceso: um pedido falhou e o display não aceita mais pedidos. */
static atomic_t display_failed;

/** @brief Pedidos enfileirados ou em execução pela thread do painel. */
static atomic_t display_pending;

/** @brief Uma linha de pixels, reaproveitada por todas as linhas de texto. */
static uint8_t row_buf[ROW_BYTES];

/**
 * @brief Procura o desenho de um caractere na fonte.
 *
 * @param c Caractere.
 * @return As DISPLAY_GLYPH_H linhas do caractere, ou NULL se a fonte não o tem.
 */
static const uint8_t *glyph_of(char c)
{
	unsigned char u = (unsigned char)c;

	if (isupper(u)) {
		return font_letters[u - 'A'];
	}
	if (isdigit(u)) {
		return font_digits[u - '0'];
	}
	if (c == '.') {
		return font_dot;
	}
	return c == ' ' ? font_space : NULL;
}

/**
 * @brief Escreve uma cor RGB565 com o byte alto primeiro.
 *
 * @param out Destino de 2 bytes.
 * @param color Cor.
 */
static void put_color(uint8_t *out, uint16_t color)
{
	out[0] = (uint8_t)(color >> 8);
	out[1] = (uint8_t)(color & 0xffU);
}

int display_format_amount(uint32_t amount, char *buf, size_t size)
{
	int len;

	if (amount > DISPLAY_MAX_AMOUNT) {
		return -EINVAL;
	}
	len = snprintf(buf, size, "BRL %" PRIu32 ".%02" PRIu32, amount / 100U, amount % 100U);
	/* Um erro (len < 0) vira um size_t enorme e também cai em -ENOSPC. */
	return (size_t)len < size ? 0 : -ENOSPC;
}

int display_render_row(const struct display_line *line, const char *text, unsigned int py,
		       uint8_t *buf, size_t size)
{
	size_t len = strlen(text);
	size_t bytes = (size_t)line->cells * DISPLAY_CELL_W(line->scale) * 2U;
	size_t pad;
	uint8_t *out = buf;

	if (line->scale == 0U || len > line->cells || py >= DISPLAY_CELL_H(line->scale)) {
		return -EINVAL;
	}
	if (bytes > size) {
		return -ENOSPC;
	}
	pad = (line->cells - len) / 2U;
	for (size_t cell = 0; cell < line->cells; cell++) {
		char c = (cell >= pad && cell - pad < len) ? text[cell - pad] : ' ';
		const uint8_t *glyph = glyph_of(c);
		unsigned int gy = py / line->scale;
		uint8_t bits;

		if (glyph == NULL) {
			return -EINVAL;
		}
		bits = gy < DISPLAY_GLYPH_H ? glyph[gy] : 0U;
		for (unsigned int px = 0; px < DISPLAY_CELL_W(line->scale); px++) {
			unsigned int gx = px / line->scale;
			bool on = gx < DISPLAY_GLYPH_W && (bits & BIT(DISPLAY_GLYPH_W - 1 - gx)) != 0U;

			put_color(out, on ? line->fg : COLOR_BG);
			out += 2;
		}
	}
	return (int)bytes;
}

/**
 * @brief Seleciona a janela de uma linha de texto com CASET e RASET.
 *
 * @param line Linha de texto.
 * @return 0 em sucesso ou o erro de hw_display_cmd().
 */
static int set_window(const struct display_line *line)
{
	uint16_t x1 = line->x + line->cells * DISPLAY_CELL_W(line->scale) - 1U;
	uint16_t y1 = line->y + DISPLAY_CELL_H(line->scale) - 1U;
	const uint8_t caset[4] = {line->x >> 8, line->x & 0xffU, x1 >> 8, x1 & 0xffU};
	const uint8_t raset[4] = {line->y >> 8, line->y & 0xffU, y1 >> 8, y1 & 0xffU};
	int err = hw_display_cmd(ST7789_CASET, caset, sizeof(caset));

	return err < 0 ? err : hw_display_cmd(ST7789_RASET, raset, sizeof(raset));
}

/**
 * @brief Desenha uma linha de texto inteira numa única janela.
 *
 * @param line Linha de texto.
 * @param text Texto, centralizado; as sobras viram fundo.
 * @return 0 em sucesso, -EINVAL para texto inválido ou o erro de hw_display_*.
 */
static int draw_line(const struct display_line *line, const char *text)
{
	int err = set_window(line);
	int len;

	if (err == 0) {
		err = hw_display_cmd(ST7789_RAMWR, NULL, 0);
	}
	for (unsigned int py = 0; err == 0 && py < DISPLAY_CELL_H(line->scale); py++) {
		len = display_render_row(line, text, py, row_buf, sizeof(row_buf));
		err = len < 0 ? len : hw_display_data(row_buf, (size_t)len);
	}
	return err;
}

/**
 * @brief Desenha as linhas de texto que têm texto novo.
 *
 * @param texts Um texto por linha; NULL deixa a linha como está.
 * @return 0 em sucesso ou o primeiro erro.
 */
static int draw_lines(const char *const texts[SLOT_COUNT])
{
	int err;

	for (size_t i = 0; i < SLOT_COUNT; i++) {
		if (texts[i] == NULL) {
			continue;
		}
		err = draw_line(&slots[i], texts[i]);
		if (err < 0) {
			return err;
		}
	}
	return 0;
}

/**
 * @brief Reset por hardware e sequência de inicialização do painel.
 *
 * @return 0 em sucesso ou o erro de hw_display_*.
 */
static int init_panel(void)
{
	int err = hw_display_reset(true);

	if (err < 0) {
		return err;
	}
	k_msleep(RESET_PULSE_MS);
	err = hw_display_reset(false);
	if (err < 0) {
		return err;
	}
	k_msleep(RESET_WAIT_MS);
	for (size_t i = 0; i < ARRAY_SIZE(init_steps); i++) {
		err = hw_display_cmd(init_steps[i].cmd, &init_steps[i].param, init_steps[i].len);
		if (err < 0) {
			return err;
		}
		k_msleep(init_steps[i].delay_ms);
	}
	return 0;
}

int display_handle(const struct display_request *req)
{
	char amount[BIG_CELLS + 1];
	const char *texts[SLOT_COUNT] = {"", "", "", ""};
	int err;

	switch (req->op) {
	case DISPLAY_OP_INIT:
		return init_panel();
	case DISPLAY_OP_PLATE:
		texts[SLOT_PLATE] = req->plate;
		break;
	case DISPLAY_OP_CHARGE:
		err = display_format_amount(req->amount, amount, sizeof(amount));
		if (err < 0) {
			return err;
		}
		texts[SLOT_PLATE] = NULL;
		texts[SLOT_AMOUNT] = amount;
		texts[SLOT_STATUS] = "SIMULATED PAYMENT";
		texts[SLOT_NOTICE] = "NOT PIX";
		break;
	case DISPLAY_OP_CLEAR:
		break;
	default:
		return -EINVAL;
	}
	return draw_lines(texts);
}

/**
 * @brief Corpo da thread do display: inicializa e depois atende a fila.
 *
 * Um pedido que falha deixa o display marcado como falho; as funções
 * públicas passam a devolver -EIO, e a faixa de saída pede assistência.
 *
 * @param p1 Não usado.
 * @param p2 Não usado.
 * @param p3 Não usado.
 */
static void display_thread(void *p1, void *p2, void *p3)
{
	const struct display_request init = {.op = DISPLAY_OP_INIT};
	struct display_request req;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	if (display_handle(&init) < 0) {
		atomic_set_bit(&display_failed, 0);
	}
	while (true) {
		if (k_msgq_get(&display_queue, &req, K_FOREVER) != 0) {
			continue;
		}
		if (display_handle(&req) < 0) {
			atomic_set_bit(&display_failed, 0);
		}
		atomic_dec(&display_pending);
	}
}

/**
 * @brief Põe um pedido na fila sem esperar.
 *
 * @param req Pedido.
 * @return 0, -ENOMEM com a fila cheia ou -EIO se o display falhou antes.
 */
static int enqueue(const struct display_request *req)
{
	if (atomic_test_bit(&display_failed, 0)) {
		return -EIO;
	}
	/* A thread pode receber o pedido imediatamente ao inserir na fila. */
	atomic_inc(&display_pending);
	if (k_msgq_put(&display_queue, req, K_NO_WAIT) != 0) {
		atomic_dec(&display_pending);
		return -ENOMEM;
	}
	return 0;
}

int display_init(void)
{
	if (display_tid != NULL) {
		return -EALREADY;
	}
	display_tid = k_thread_create(&display_thread_data, display_stack,
				      K_THREAD_STACK_SIZEOF(display_stack), display_thread, NULL,
				      NULL, NULL, DISPLAY_THREAD_PRIORITY, 0, K_NO_WAIT);
	return 0;
}

int display_show_plate(const char *plate)
{
	struct display_request req = {.op = DISPLAY_OP_PLATE};

	if (plate == NULL || strnlen(plate, DISPLAY_PLATE_LEN + 1) != DISPLAY_PLATE_LEN ||
	    strspn(plate, plate_chars) != DISPLAY_PLATE_LEN) {
		return -EINVAL;
	}
	memcpy(req.plate, plate, DISPLAY_PLATE_LEN);
	return enqueue(&req);
}

int display_show_charge(uint32_t amount)
{
	struct display_request req = {.op = DISPLAY_OP_CHARGE, .amount = amount};

	if (amount > DISPLAY_MAX_AMOUNT) {
		return -EINVAL;
	}
	return enqueue(&req);
}

int display_status(void)
{
	if (atomic_get(&display_pending) != 0) {
		return -EAGAIN;
	}
	return atomic_test_bit(&display_failed, 0) ? -EIO : 0;
}

int display_clear(void)
{
	const struct display_request req = {.op = DISPLAY_OP_CLEAR};

	return enqueue(&req);
}
