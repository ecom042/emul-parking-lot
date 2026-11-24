/**
 * @file test_display.c
 * @brief Testes do display (display.c) com o SPI falso do FFF.
 *
 * hw_display_cmd() e hw_display_data() alimentam um painel falso que registra
 * cada janela (CASET/RASET), os bytes enviados depois do RAMWR e quantos
 * pixels acesos (diferentes do fundo preto) ela recebeu.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "display.h"
#include "hw.h"

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, hw_display_reset, bool);
FAKE_VALUE_FUNC(int, hw_display_cmd, uint8_t, const uint8_t *, size_t);
FAKE_VALUE_FUNC(int, hw_display_data, const uint8_t *, size_t);

#define MAX_WINDOWS 64
#define MAX_CMDS    256

/** @brief Uma janela desenhada no painel falso. */
struct window {
	uint16_t x0, x1, y0, y1; /**< Coordenadas pedidas por CASET e RASET. */
	bool ramwr;              /**< RAMWR veio depois de CASET e RASET. */
	size_t bytes;            /**< Bytes de pixel recebidos. */
	size_t on;               /**< Pixels diferentes do fundo. */
};

/** @brief Painel falso. */
static struct {
	struct window win[MAX_WINDOWS]; /**< Janelas, na ordem. */
	size_t count;                   /**< Janelas registradas. */
	uint8_t cmds[MAX_CMDS];         /**< Comandos recebidos, na ordem. */
	uint8_t params[MAX_CMDS];       /**< Primeiro parâmetro de cada comando, ou 0. */
	size_t ncmds;                   /**< Comandos registrados. */
} panel;

/**
 * @brief Lê um valor de 16 bits com o byte alto primeiro.
 *
 * @param p Dois bytes.
 * @return O valor.
 */
static uint16_t be16(const uint8_t *p)
{
	return (uint16_t)((p[0] << 8) | p[1]);
}

/**
 * @brief Falso de hw_display_cmd(): registra comandos e janelas.
 *
 * @param cmd Comando.
 * @param params Parâmetros.
 * @param len Quantidade de parâmetros.
 * @return 0 (o erro vem de return_val_seq, quando o teste pede).
 */
static int fake_cmd(uint8_t cmd, const uint8_t *params, size_t len)
{
	struct window *w = &panel.win[panel.count > 0 ? panel.count - 1 : 0];

	zassert_true(panel.ncmds < MAX_CMDS);
	panel.cmds[panel.ncmds] = cmd;
	panel.params[panel.ncmds] = len > 0 ? params[0] : 0;
	panel.ncmds++;
	switch (cmd) {
	case 0x2a:
		zassert_equal(len, 4);
		zassert_true(panel.count < MAX_WINDOWS);
		w = &panel.win[panel.count++];
		memset(w, 0, sizeof(*w));
		w->x0 = be16(params);
		w->x1 = be16(params + 2);
		break;
	case 0x2b:
		zassert_equal(len, 4);
		w->y0 = be16(params);
		w->y1 = be16(params + 2);
		break;
	case 0x2c:
		zassert_equal(len, 0);
		w->ramwr = true;
		break;
	default:
		break;
	}
	return 0;
}

/**
 * @brief Falso de hw_display_data(): conta bytes e pixels acesos da janela atual.
 *
 * @param data Bytes de pixel.
 * @param len Quantidade de bytes.
 * @return hw_display_data_fake.return_val.
 */
static int fake_data(const uint8_t *data, size_t len)
{
	struct window *w;

	if (hw_display_data_fake.return_val != 0) {
		return hw_display_data_fake.return_val;
	}
	zassert_true(panel.count > 0, "pixels antes de qualquer janela");
	w = &panel.win[panel.count - 1];
	zassert_true(w->ramwr, "pixels sem RAMWR");
	zassert_equal(len % 2, 0, "pixel RGB565 incompleto");
	for (size_t i = 0; i < len; i += 2) {
		w->on += be16(data + i) != 0 ? 1 : 0;
	}
	w->bytes += len;
	return 0;
}

/**
 * @brief Restaura falsos e painel.
 *
 * @param fixture Não usado.
 */
static void reset_panel(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(hw_display_reset);
	RESET_FAKE(hw_display_cmd);
	RESET_FAKE(hw_display_data);
	FFF_RESET_HISTORY();
	hw_display_data_fake.custom_fake = fake_data;
	memset(&panel, 0, sizeof(panel));
}

ZTEST_SUITE(display, NULL, NULL, reset_panel, NULL, NULL);

/**
 * @brief Usa o painel falso em hw_display_cmd(), sem erro.
 */
static void use_panel(void)
{
	hw_display_cmd_fake.custom_fake = fake_cmd;
}

/**
 * @brief Pixels acesos que um texto deve produzir numa janela.
 *
 * A geometria vem da própria janela: altura 8 x escala, largura 6 x escala x
 * células.
 *
 * @param w Janela.
 * @param text Texto esperado.
 * @return Pixels acesos.
 */
static size_t expected_on(const struct window *w, const char *text)
{
	static uint8_t row[DISPLAY_WIDTH * 2];
	unsigned int height = w->y1 - w->y0 + 1U;
	unsigned int scale = height / (DISPLAY_GLYPH_H + 1);
	struct display_line line = {
		.x = w->x0,
		.y = w->y0,
		.scale = (uint8_t)scale,
		.cells = (uint8_t)((w->x1 - w->x0 + 1U) / DISPLAY_CELL_W(scale)),
		.fg = 0xffff,
	};
	size_t on = 0;

	for (unsigned int py = 0; py < height; py++) {
		int len = display_render_row(&line, text, py, row, sizeof(row));

		zassert_true(len > 0);
		for (int i = 0; i < len; i += 2) {
			on += be16(row + i) != 0 ? 1 : 0;
		}
	}
	return on;
}

/**
 * @brief Confere que a janela i é válida, recebeu todos os pixels e mostra o texto.
 *
 * @param i Índice da janela.
 * @param text Texto esperado.
 */
static void check_window(size_t i, const char *text)
{
	const struct window *w = &panel.win[i];

	zassert_true(i < panel.count, "janela %u não desenhada", (unsigned int)i);
	zassert_true(w->x0 <= w->x1 && w->x1 < DISPLAY_WIDTH, "colunas inválidas");
	zassert_true(w->y0 <= w->y1 && w->y1 < DISPLAY_HEIGHT, "linhas inválidas");
	zassert_true(w->ramwr);
	zassert_equal(w->bytes, (size_t)(w->x1 - w->x0 + 1) * (w->y1 - w->y0 + 1) * 2,
		      "janela %u incompleta", (unsigned int)i);
	zassert_equal(w->on, expected_on(w, text), "janela %u não mostra \"%s\"",
		      (unsigned int)i, text);
}

ZTEST(display, test_format_amount)
{
	char buf[16];

	zassert_ok(display_format_amount(400, buf, sizeof(buf)));
	zassert_str_equal(buf, "BRL 4.00");
	zassert_ok(display_format_amount(0, buf, sizeof(buf)));
	zassert_str_equal(buf, "BRL 0.00");
	zassert_ok(display_format_amount(5, buf, sizeof(buf)));
	zassert_str_equal(buf, "BRL 0.05");
	zassert_ok(display_format_amount(1234, buf, sizeof(buf)));
	zassert_str_equal(buf, "BRL 12.34");
	zassert_ok(display_format_amount(DISPLAY_MAX_AMOUNT, buf, sizeof(buf)));
	zassert_str_equal(buf, "BRL 10000.00");
	zassert_equal(display_format_amount(DISPLAY_MAX_AMOUNT + 1, buf, sizeof(buf)), -EINVAL);
	zassert_equal(display_format_amount(400, buf, sizeof("BRL 4.00") - 1), -ENOSPC);
	zassert_ok(display_format_amount(400, buf, sizeof("BRL 4.00")));
}

ZTEST(display, test_render_row_pixels)
{
	const struct display_line line = {.scale = 1, .cells = 1, .fg = 0xf800};
	uint8_t row[2 * DISPLAY_CELL_W(1)];
	/* "1": linha 0 do desenho é ..#.. e a linha 6 é .###. */
	const uint8_t top[] = {0, 0, 0, 0, 0xf8, 0, 0, 0, 0, 0, 0, 0};
	const uint8_t bottom[] = {0, 0, 0xf8, 0, 0xf8, 0, 0xf8, 0, 0, 0, 0, 0};
	const uint8_t gap[sizeof(row)] = {0};

	zassert_equal(display_render_row(&line, "1", 0, row, sizeof(row)), sizeof(row));
	zassert_mem_equal(row, top, sizeof(row));
	zassert_equal(display_render_row(&line, "1", 6, row, sizeof(row)), sizeof(row));
	zassert_mem_equal(row, bottom, sizeof(row));
	zassert_equal(display_render_row(&line, "1", 7, row, sizeof(row)), sizeof(row));
	zassert_mem_equal(row, gap, sizeof(row), "linha de espaço abaixo do caractere");
}

ZTEST(display, test_render_row_scale_and_center)
{
	const struct display_line line = {.scale = 2, .cells = 3, .fg = 0xffff};
	uint8_t row[3 * 2 * DISPLAY_CELL_W(2)];
	uint8_t first[2 * DISPLAY_CELL_W(2)];
	uint8_t second[2 * DISPLAY_CELL_W(2)];

	zassert_equal(display_render_row(&line, "1", 0, row, sizeof(row)), sizeof(row));
	memset(first, 0, sizeof(first));
	zassert_mem_equal(row, first, sizeof(first), "o texto é centralizado");
	zassert_equal(row[sizeof(first) + 8], 0xff, "coluna do meio do 1, ampliada");
	zassert_equal(row[sizeof(first) + 10], 0xff);
	zassert_equal(row[sizeof(first) + 6], 0x00);
	memcpy(first, row, sizeof(first));
	zassert_equal(display_render_row(&line, "1", 1, row, sizeof(row)), sizeof(row));
	memcpy(second, row, sizeof(second));
	zassert_mem_equal(first, second, sizeof(first), "cada linha da fonte vira duas");
}

ZTEST(display, test_render_row_every_char)
{
	const struct display_line line = {.scale = 1, .cells = 1, .fg = 0xffff};
	const char *chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.";
	uint8_t row[2 * DISPLAY_CELL_W(1)];
	char text[2] = {0};

	for (const char *c = chars; *c != '\0'; c++) {
		size_t on = 0;

		text[0] = *c;
		for (unsigned int py = 0; py < DISPLAY_CELL_H(1); py++) {
			zassert_equal(display_render_row(&line, text, py, row, sizeof(row)),
				      sizeof(row), "caractere %c", *c);
			for (size_t i = 0; i < sizeof(row); i++) {
				on += row[i] != 0 ? 1 : 0;
			}
		}
		zassert_true(on > 0, "caractere %c sem desenho", *c);
	}
}

ZTEST(display, test_render_row_errors)
{
	struct display_line line = {.scale = 1, .cells = 2, .fg = 0xffff};
	uint8_t row[2 * 2 * DISPLAY_CELL_W(1)];

	zassert_equal(display_render_row(&line, "ABC", 0, row, sizeof(row)), -EINVAL);
	zassert_equal(display_render_row(&line, "a", 0, row, sizeof(row)), -EINVAL);
	zassert_equal(display_render_row(&line, "-", 0, row, sizeof(row)), -EINVAL);
	zassert_equal(display_render_row(&line, "A", DISPLAY_CELL_H(1), row, sizeof(row)),
		      -EINVAL);
	zassert_equal(display_render_row(&line, "A", 0, row, sizeof(row) - 1), -ENOSPC);
	zassert_equal(display_render_row(&line, "", 0, row, sizeof(row)), sizeof(row));
	line.scale = 0;
	zassert_equal(display_render_row(&line, "A", 0, row, sizeof(row)), -EINVAL);
}

ZTEST(display, test_init_sequence)
{
	const struct display_request req = {.op = DISPLAY_OP_INIT};
	const uint8_t cmds[] = {0x01, 0x11, 0x3a, 0x36, 0x29};
	int64_t start = k_uptime_get();

	use_panel();
	zassert_ok(display_handle(&req));
	zassert_true(k_uptime_get() - start >= 10 + 120 + 150 + 120, "atrasos da inicialização");
	zassert_equal(hw_display_reset_fake.call_count, 2);
	zassert_true(hw_display_reset_fake.arg0_history[0], "reset acionado primeiro");
	zassert_false(hw_display_reset_fake.arg0_history[1], "depois solto");
	zassert_equal(panel.ncmds, sizeof(cmds));
	zassert_mem_equal(panel.cmds, cmds, sizeof(cmds));
	zassert_equal(panel.params[2], 0x55, "COLMOD RGB565");
	zassert_equal(panel.params[3], 0x00, "MADCTL nativo");
	zassert_equal(hw_display_cmd_fake.arg2_history[2], 1);
	zassert_equal(hw_display_cmd_fake.arg2_history[0], 0, "SWRESET sem parâmetro");
}

ZTEST(display, test_init_errors)
{
	const struct display_request req = {.op = DISPLAY_OP_INIT};
	int reset_seq[] = {0, -EIO};
	int cmd_seq[] = {0, 0, -EIO};

	hw_display_reset_fake.return_val = -EIO;
	zassert_equal(display_handle(&req), -EIO);
	zassert_equal(hw_display_cmd_fake.call_count, 0);

	RESET_FAKE(hw_display_reset);
	SET_RETURN_SEQ(hw_display_reset, reset_seq, 2);
	zassert_equal(display_handle(&req), -EIO);
	zassert_equal(hw_display_reset_fake.call_count, 2);
	zassert_equal(hw_display_cmd_fake.call_count, 0);

	RESET_FAKE(hw_display_reset);
	SET_RETURN_SEQ(hw_display_cmd, cmd_seq, 3);
	zassert_equal(display_handle(&req), -EIO);
	zassert_equal(hw_display_cmd_fake.call_count, 3, "para no COLMOD que falhou");
}

ZTEST(display, test_plate_screen)
{
	struct display_request req = {.op = DISPLAY_OP_PLATE};

	memcpy(req.plate, "ABC1D23", sizeof(req.plate));
	use_panel();
	zassert_ok(display_handle(&req));
	zassert_equal(panel.count, 4, "placa e três linhas apagadas");
	check_window(0, "ABC1D23");
	check_window(1, "");
	check_window(2, "");
	check_window(3, "");
	zassert_true(panel.win[0].on > 0);
	zassert_equal(panel.win[1].on + panel.win[2].on + panel.win[3].on, 0);
}

ZTEST(display, test_charge_screen)
{
	const struct display_request req = {.op = DISPLAY_OP_CHARGE, .amount = 400};
	size_t area = 0;

	use_panel();
	zassert_ok(display_handle(&req));
	zassert_equal(panel.count, 3, "a placa continua; valor, aviso e NOT PIX");
	check_window(0, "BRL 4.00");
	check_window(1, "SIMULATED PAYMENT");
	check_window(2, "NOT PIX");
	for (size_t i = 0; i < panel.count; i++) {
		zassert_true(panel.win[i].y0 > panel.win[0].y0 || i == 0, "linhas de cima para baixo");
		area += panel.win[i].bytes / 2;
	}
	zassert_true(area < DISPLAY_WIDTH * DISPLAY_HEIGHT / 4, "só regiões pequenas");
}

ZTEST(display, test_clear_screen)
{
	const struct display_request req = {.op = DISPLAY_OP_CLEAR};

	use_panel();
	zassert_ok(display_handle(&req));
	zassert_equal(panel.count, 4);
	for (size_t i = 0; i < panel.count; i++) {
		check_window(i, "");
	}
}

ZTEST(display, test_handle_invalid_requests)
{
	struct display_request req = {.op = DISPLAY_OP_PLATE};

	use_panel();
	memcpy(req.plate, "abc1d23", sizeof(req.plate));
	zassert_equal(display_handle(&req), -EINVAL, "a fonte não tem minúsculas");
	req.op = DISPLAY_OP_CHARGE;
	req.amount = DISPLAY_MAX_AMOUNT + 1;
	zassert_equal(display_handle(&req), -EINVAL);
	req.op = (enum display_op)99;
	zassert_equal(display_handle(&req), -EINVAL);
}

ZTEST(display, test_draw_errors)
{
	const struct display_request req = {.op = DISPLAY_OP_CLEAR};

	for (int failing = 0; failing < 3; failing++) {
		int seq[3] = {0, 0, 0};

		RESET_FAKE(hw_display_cmd);
		seq[failing] = -EIO;
		SET_RETURN_SEQ(hw_display_cmd, seq, 3);
		zassert_equal(display_handle(&req), -EIO, "falha no comando %d", failing);
		zassert_equal(hw_display_cmd_fake.call_count, failing + 1);
		zassert_equal(hw_display_data_fake.call_count, 0);
	}

	RESET_FAKE(hw_display_cmd);
	hw_display_data_fake.return_val = -EIO;
	zassert_equal(display_handle(&req), -EIO);
	zassert_equal(hw_display_data_fake.call_count, 1, "para no primeiro erro de dados");
	zassert_equal(hw_display_cmd_fake.call_count, 3);
}

ZTEST(display, test_show_validates_arguments)
{
	zassert_equal(display_show_plate(NULL), -EINVAL);
	zassert_equal(display_show_plate("ABC1D2"), -EINVAL);
	zassert_equal(display_show_plate("ABC1D234"), -EINVAL);
	zassert_equal(display_show_plate("abc1d23"), -EINVAL);
	zassert_equal(display_show_plate("ABC-D23"), -EINVAL);
	zassert_equal(display_show_charge(DISPLAY_MAX_AMOUNT + 1), -EINVAL);
}

/*
 * A thread é criada uma só vez no binário, por isso o ciclo de vida inteiro
 * fica num teste: fila cheia, inicialização, pedidos atendidos e falha.
 */
ZTEST(display, test_thread_lifecycle)
{
	int queued = 0;

	use_panel();
	while (display_clear() == 0) {
		queued++;
		zassert_true(queued <= 64, "a fila nunca enche");
	}
	zassert_true(queued > 0);
	zassert_equal(display_clear(), -ENOMEM, "fila cheia");
	zassert_equal(display_status(), -EAGAIN);

	zassert_ok(display_init());
	zassert_equal(display_init(), -EALREADY);
	zassert_equal(hw_display_reset_fake.call_count, 0, "a thread roda abaixo do teste");

	k_msleep(5);
	zassert_equal(hw_display_reset_fake.call_count, 1, "reset acionado, esperando 10 ms");
	k_msleep(1000);
	zassert_equal(hw_display_reset_fake.call_count, 2);
	zassert_mem_equal(panel.cmds, ((const uint8_t[]){0x01, 0x11, 0x3a, 0x36, 0x29}), 5);
	zassert_ok(display_status());
	zassert_equal(panel.count, (size_t)queued * 4, "os pedidos da fila foram atendidos");

	memset(&panel, 0, sizeof(panel));
	zassert_ok(display_show_plate("ABC1D23"));
	zassert_ok(display_show_charge(400));
	zassert_equal(panel.count, 0, "as funções só enfileiram");
	zassert_equal(display_status(), -EAGAIN);
	k_msleep(200);
	zassert_ok(display_status());
	zassert_equal(panel.count, 7);
	check_window(0, "ABC1D23");
	check_window(4, "BRL 4.00");
	check_window(5, "SIMULATED PAYMENT");
	check_window(6, "NOT PIX");

	hw_display_cmd_fake.custom_fake = NULL;
	hw_display_cmd_fake.return_val = -EIO;
	zassert_ok(display_clear());
	k_msleep(50);
	zassert_equal(display_status(), -EIO);
	zassert_equal(display_clear(), -EIO, "display falho recusa pedidos");
	zassert_equal(display_show_plate("ABC1D23"), -EIO);
	zassert_equal(display_show_charge(400), -EIO);
}
