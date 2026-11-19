/*
 * test_display.c — máquina de estados do display (display.c): REQ-12.
 *
 * COMO FUNCIONA O "PAINEL FALSO" DESTE ARQUIVO
 *   display.c só fala com o hardware por display_hw_* (reset, command, data).
 *   Aqui essas funções são falsos FFF que alimentam `panel`, uma PORTA PARA C das
 *   regras do modelo real do curso (models/ST7789V.cs):
 *     - byte SPI com CS alto, em reset ou antes da borda de descida do reset => erro;
 *     - RAMWR antes da inicialização completa (SLPOUT + DISPON + RGB565 + MADCTL 0) => erro;
 *     - comando no meio de parâmetros ou de um pixel pela metade => erro;
 *     - pixels gravados no framebuffer de 240x320 em RGB565.
 *   O teste pode então exigir `panel.errors == 0`, a ordem dos comandos, os
 *   instantes (ms virtuais) de cada passo da inicialização e, com um "OCR" que
 *   decodifica o framebuffer de volta em letras, QUE TEXTO apareceu e ONDE.
 *
 * FALHAS DE HARDWARE: `fail_at = k` faz a k-ésima chamada de hardware depois de
 * armada falhar com -EIO (caminhos que os cenários nunca provocam).
 */
#include <errno.h>
#include <string.h>

#include <zephyr/fff.h>
DEFINE_FFF_GLOBALS;
#include <zephyr/ztest.h>

#include "display.h"
#include "display_hw.h"
#include "display_render.h"
#include "font5x7.h"

FAKE_VALUE_FUNC(int, display_hw_init);
FAKE_VALUE_FUNC(int, display_hw_reset, bool);
FAKE_VALUE_FUNC(int, display_hw_command, uint8_t, const uint8_t *, size_t);
FAKE_VALUE_FUNC(int, display_hw_data, const uint8_t *, size_t);

#define PLATE "ABC1D23"

/* ---------------- painel falso (porta de models/ST7789V.cs) ---------------- */

static struct {
	bool cs, dc, reset_pin, reset_seen, awake, display_on;
	uint8_t command, format, orientation;
	int needed, received, x0, x1, y0, y1, x, y, high_byte;
	uint8_t parameters[4];
	uint16_t pixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
	unsigned int errors, pixel_writes, hardware_resets;
	char commands[160]; /* "01 11 3A ..." */
} panel;

static void panel_clear_controller(void)
{
	panel.awake = panel.display_on = false;
	panel.format = 0x66U;
	panel.orientation = 0U;
	panel.command = 0U;
	panel.needed = panel.received = 0;
	panel.x0 = panel.y0 = panel.x = panel.y = 0;
	panel.x1 = (int)DISPLAY_WIDTH - 1;
	panel.y1 = (int)DISPLAY_HEIGHT - 1;
	panel.high_byte = -1;
}

static void panel_power_on(void)
{
	memset(&panel, 0, sizeof(panel));
	panel.cs = true; /* CS repousa em nível alto (desselecionado) */
	panel_clear_controller();
}

static bool panel_initialized(void)
{
	return panel.awake && panel.display_on && panel.format == 0x55U && panel.orientation == 0U;
}

static void panel_reject(void)
{
	panel.errors++;
}

static void panel_command(uint8_t value)
{
	char hex[4];

	if (panel.received != panel.needed || panel.high_byte >= 0) {
		panel_reject();
	}
	panel.command = value;
	panel.needed = panel.received = 0;
	panel.high_byte = -1;
	snprintf(hex, sizeof(hex), "%02X ", value);
	strncat(panel.commands, hex, sizeof(panel.commands) - strlen(panel.commands) - 1U);
	if (value == 0x01U) { /* SWRESET */
		panel.awake = panel.display_on = false;
		panel.command = 0U;
		panel.x = panel.x0;
		panel.y = panel.y0;
	} else if (value == 0x11U) { /* SLPOUT */
		panel.awake = true;
	} else if (value == 0x28U) {
		panel.display_on = false;
	} else if (value == 0x29U) { /* DISPON */
		panel.display_on = true;
	} else if (value == 0x3AU || value == 0x36U) { /* COLMOD, MADCTL */
		panel.needed = 1;
	} else if (value == 0x2AU || value == 0x2BU) { /* CASET, RASET */
		panel.needed = 4;
	} else if (value == 0x2CU) { /* RAMWR */
		if (!panel_initialized()) {
			panel_reject();
		}
		panel.x = panel.x0;
		panel.y = panel.y0;
	} else {
		panel.command = 0U;
		panel_reject();
	}
}

static void panel_data(uint8_t value)
{
	if (panel.command == 0x2CU) {
		if (!panel_initialized() || panel.x0 > panel.x1 || panel.y0 > panel.y1 ||
		    panel.x1 >= (int)DISPLAY_WIDTH || panel.y1 >= (int)DISPLAY_HEIGHT) {
			panel_reject();
			return;
		}
		if (panel.high_byte < 0) {
			panel.high_byte = value;
			return;
		}
		panel.pixels[panel.y * (int)DISPLAY_WIDTH + panel.x] =
			(uint16_t)((panel.high_byte << 8) | value);
		panel.high_byte = -1;
		panel.pixel_writes++;
		if (++panel.x > panel.x1) {
			panel.x = panel.x0;
			if (++panel.y > panel.y1) {
				panel.y = panel.y0;
			}
		}
		return;
	}
	if (panel.received >= panel.needed) {
		panel_reject();
		return;
	}
	panel.parameters[panel.received++] = value;
	if (panel.received != panel.needed) {
		return;
	}
	if (panel.command == 0x3AU) {
		panel.format = value;
		if (value != 0x55U) {
			panel_reject();
		}
	} else if (panel.command == 0x36U) {
		panel.orientation = value;
		if (value != 0U) {
			panel_reject();
		}
	} else if (panel.command == 0x2AU) {
		panel.x0 = (panel.parameters[0] << 8) | panel.parameters[1];
		panel.x1 = (panel.parameters[2] << 8) | panel.parameters[3];
	} else if (panel.command == 0x2BU) {
		panel.y0 = (panel.parameters[0] << 8) | panel.parameters[1];
		panel.y1 = (panel.parameters[2] << 8) | panel.parameters[3];
	}
}

static void panel_transmit(uint8_t value)
{
	if (panel.cs || !panel.reset_pin || !panel.reset_seen) {
		panel_reject(); /* byte com CS alto, em reset, ou antes do reset por hardware */
		return;
	}
	if (panel.dc) {
		panel_data(value);
	} else {
		panel_command(value);
	}
}

/* O pino de reset (nível físico): a borda de descida limpa o controlador. */
static void panel_reset_pin(bool level)
{
	if (panel.reset_pin && !level) {
		panel_clear_controller();
		panel.hardware_resets++;
		panel.reset_seen = true;
	}
	panel.reset_pin = level;
}

/* ---------------- falsos do hardware ---------------- */

static int hw_init_result;
static unsigned int fail_at;     /* 0 = nunca falha; k = a k-ésima chamada depois de armada */
static unsigned int fail_count;

static bool should_fail(void)
{
	fail_count++;
	return fail_at != 0U && fail_count == fail_at;
}

static int fake_init(void)
{
	if (hw_init_result == 0) {
		panel.dc = false;
		panel_reset_pin(true); /* repouso: reset SOLTO (nível alto) */
	}
	return hw_init_result;
}

static int fake_reset(bool asserted)
{
	if (should_fail()) {
		return -EIO;
	}
	panel_reset_pin(!asserted);
	return 0;
}

static int fake_command(uint8_t command, const uint8_t *params, size_t length)
{
	if (should_fail()) {
		return -EIO;
	}
	panel.cs = false;
	panel.dc = false;
	panel_transmit(command);
	if (length > 0U) {
		panel.dc = true;
		for (size_t i = 0U; i < length; ++i) {
			panel_transmit(params[i]);
		}
	}
	panel.cs = true;
	return 0;
}

static int fake_data(const uint8_t *data, size_t length)
{
	if (should_fail()) {
		return -EIO;
	}
	panel.cs = false;
	panel.dc = true;
	for (size_t i = 0U; i < length; ++i) {
		panel_transmit(data[i]);
	}
	panel.cs = true;
	return 0;
}

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(display_hw_init);
	RESET_FAKE(display_hw_reset);
	RESET_FAKE(display_hw_command);
	RESET_FAKE(display_hw_data);
	FFF_RESET_HISTORY();
	display_hw_init_fake.custom_fake = fake_init;
	display_hw_reset_fake.custom_fake = fake_reset;
	display_hw_command_fake.custom_fake = fake_command;
	display_hw_data_fake.custom_fake = fake_data;
	hw_init_result = 0;
	fail_at = 0U;
	fail_count = 0U;
	panel_power_on();
}

ZTEST_SUITE(display, NULL, NULL, before_each, NULL, NULL);

/* ---------------- auxiliares ---------------- */

/* Inicializa e avança o relógio até a inicialização terminar (os 400 ms de esperas). */
static int64_t init_display(void)
{
	zassert_ok(display_init());
	for (int64_t now = 0; now <= 500; now += 10) {
		display_service(now);
	}
	zassert_true(panel_initialized());
	return 500;
}

/* Faz poll até o desenho terminar; devolve quantas chamadas foram necessárias. */
static unsigned int poll_until_done(int64_t *now, enum equipment_result *result)
{
	unsigned int calls = 0U;

	do {
		*now += 5;
		*result = display_poll(*now);
		calls++;
	} while (*result == EQUIPMENT_PENDING && calls < 100U);
	return calls;
}

/* "OCR": decodifica `chars` letras do framebuffer a partir de (x, y), na escala dada. */
static void ocr(unsigned int x, unsigned int y, unsigned int chars, unsigned int scale,
		char out[DISPLAY_TEXT_MAX])
{
	static const char charset[] = " .0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

	for (unsigned int c = 0U; c < chars; ++c) {
		char found = '?';

		for (size_t k = 0U; charset[k] != '\0'; ++k) {
			const struct font_glyph *glyph = font_lookup(charset[k]);
			bool match = true;

			for (unsigned int row = 0U; row < FONT_ROWS && match; ++row) {
				for (unsigned int col = 0U; col < FONT_COLS && match; ++col) {
					unsigned int px = x + (c * DISPLAY_CELL_COLS + col) * scale;
					unsigned int py = y + row * scale;
					bool lit = panel.pixels[py * DISPLAY_WIDTH + px] != 0U;

					match = lit == font_pixel(glyph, col, row);
				}
			}
			if (match) {
				found = charset[k];
				break;
			}
		}
		out[c] = found;
	}
	out[chars] = '\0';
}

static void assert_text(const struct display_item *item, const char *expected)
{
	char seen[DISPLAY_TEXT_MAX];

	ocr(item->x, item->y, item->chars, item->scale, seen);
	zassert_str_equal(seen, expected);
}

static unsigned int area(const struct display_item *items, size_t count)
{
	unsigned int total = 0U;

	for (size_t i = 0U; i < count; ++i) {
		total += (unsigned int)display_item_width(&items[i]) * display_item_height(&items[i]);
	}
	return total;
}

/* ---------------- inicialização ---------------- */

ZTEST(display, test_init_sequence_has_the_contract_order_and_400_ms_of_waits)
{
	zassert_ok(display_init());
	zassert_equal(panel.hardware_resets, 0U);

	display_service(1000); /* t0: segura o reset */
	zassert_equal(panel.hardware_resets, 1U);
	zassert_false(panel.reset_pin);
	display_service(1009); /* ainda dentro dos 10 ms */
	zassert_false(panel.reset_pin);
	display_service(1010); /* solta o reset */
	zassert_true(panel.reset_pin);
	display_service(1129); /* 120 ms depois da soltura: ainda nada de comando */
	zassert_str_equal(panel.commands, "");
	display_service(1130);
	zassert_str_equal(panel.commands, "01 "); /* SWRESET */
	display_service(1279); /* espera de 150 ms */
	zassert_str_equal(panel.commands, "01 ");
	display_service(1280);
	zassert_str_equal(panel.commands, "01 11 "); /* SLPOUT */
	display_service(1399); /* espera de 120 ms */
	zassert_str_equal(panel.commands, "01 11 ");
	zassert_false(panel_initialized());
	display_service(1400); /* COLMOD, MADCTL e DISPON seguidos */
	zassert_str_equal(panel.commands, "01 11 3A 36 29 ");
	zassert_true(panel_initialized());
	zassert_equal(panel.errors, 0U); /* o modelo aceitou tudo */
	zassert_equal(panel.pixel_writes, 0U);
}

ZTEST(display, test_hw_init_failure_leaves_display_off)
{
	hw_init_result = -ENODEV;
	zassert_equal(display_init(), -ENODEV);
	zassert_equal(display_draw_plate(PLATE), -ENODEV);
	zassert_equal(display_poll(0), EQUIPMENT_FAILED);
}

ZTEST(display, test_draw_before_display_init_is_refused)
{
	zassert_equal(display_draw_plate(PLATE), -ENODEV);
	zassert_equal(display_draw_charge(PLATE, 400U), -ENODEV);
}

ZTEST(display, test_every_init_step_failure_fails_the_display)
{
	for (unsigned int step = 1U; step <= 7U; ++step) {
		panel_power_on();
		fail_count = 0U;
		fail_at = step; /* 1 reset, 2 solta, 3 SWRESET, 4 SLPOUT, 5 COLMOD, 6 MADCTL, 7 DISPON */
		zassert_ok(display_init());
		zassert_ok(display_draw_plate(PLATE)); /* pedido antes da falha: fica esperando */
		for (int64_t now = 0; now <= 500; now += 10) {
			display_service(now);
		}
		zassert_equal(display_poll(600), EQUIPMENT_FAILED, "passo %u", step);
		zassert_equal(display_draw_plate(PLATE), -EIO);
		zassert_equal(display_draw_charge(PLATE, 400U), -EIO);
		zassert_false(panel_initialized());
	}
}

/* ---------------- telas ---------------- */

ZTEST(display, test_plate_screen_is_drawn_after_init_and_read_back_by_ocr)
{
	struct display_item items[DISPLAY_MAX_ITEMS];
	enum equipment_result result;
	int64_t now = 0;

	zassert_ok(display_init());
	zassert_ok(display_draw_plate(PLATE)); /* pedido ANTES de a inicialização terminar */
	zassert_equal(display_poll(0), EQUIPMENT_PENDING);
	zassert_equal(panel.pixel_writes, 0U); /* nada é pintado antes de estar pronto */
	zassert_equal(poll_until_done(&now, &result) > 0U, true);
	now = 500;
	for (unsigned int i = 0U; i < 100U && result != EQUIPMENT_OK; ++i) {
		now += 10;
		result = display_poll(now);
	}
	zassert_equal(result, EQUIPMENT_OK);
	zassert_equal(panel.errors, 0U);
	zassert_equal(display_layout_plate(PLATE, items), 2U);
	assert_text(&items[0], PLATE);
	zassert_equal(panel.pixel_writes, area(items, 2U)); /* só os 2 retângulos, não a tela toda */
	zassert_true(panel.pixel_writes < DISPLAY_WIDTH * DISPLAY_HEIGHT / 6U);
	zassert_equal(display_poll(now + 10), EQUIPMENT_OK); /* continua OK até novo pedido */
}

ZTEST(display, test_charge_screen_shows_plate_amount_simulated_payment_and_not_pix)
{
	struct display_item items[DISPLAY_MAX_ITEMS];
	enum equipment_result result;
	int64_t now = init_display();

	zassert_ok(display_draw_charge(PLATE, 400U));
	poll_until_done(&now, &result);
	zassert_equal(result, EQUIPMENT_OK);
	zassert_equal(panel.errors, 0U);
	zassert_equal(display_layout_charge(PLATE, 400U, items), 4U);
	assert_text(&items[0], PLATE);
	assert_text(&items[1], "BRL 4.00    ");
	assert_text(&items[2], "SIMULATED PAYMENT");
	assert_text(&items[3], "NOT PIX");
	zassert_equal(panel.pixel_writes, area(items, 4U));
	/* cores certas: um pixel aceso do valor é verde; fora dos retângulos continua preto */
	zassert_equal(panel.pixels[items[1].y * DISPLAY_WIDTH + items[1].x], DISPLAY_COLOR_GREEN);
	zassert_equal(panel.pixels[0], 0U);
}

ZTEST(display, test_new_amount_fully_overwrites_the_previous_one)
{
	struct display_item items[DISPLAY_MAX_ITEMS];
	enum equipment_result result;
	int64_t now = init_display();

	zassert_ok(display_draw_charge("XYZ9Z99", 1000000U)); /* "BRL 10000.00": o mais largo */
	poll_until_done(&now, &result);
	zassert_equal(result, EQUIPMENT_OK);
	zassert_ok(display_draw_plate(PLATE)); /* próxima saída: a tela da placa apaga o valor */
	poll_until_done(&now, &result);
	zassert_equal(display_layout_plate(PLATE, items), 2U);
	assert_text(&items[0], PLATE);
	assert_text(&items[1], "            ");
	zassert_ok(display_draw_charge(PLATE, 400U));
	poll_until_done(&now, &result);
	zassert_equal(display_layout_charge(PLATE, 400U, items), 4U);
	assert_text(&items[1], "BRL 4.00    "); /* sem restos de "10000" */
	zassert_equal(panel.errors, 0U);
}

ZTEST(display, test_each_service_call_does_a_bounded_slice_of_work)
{
	enum equipment_result result = EQUIPMENT_PENDING;
	unsigned int calls = 0U;
	int64_t now = init_display();

	zassert_ok(display_draw_charge(PLATE, 400U));
	while (result == EQUIPMENT_PENDING && calls < 100U) {
		unsigned int before = panel.pixel_writes;

		now += 5;
		result = display_poll(now);
		calls++;
		/* no máximo DISPLAY_ROWS_PER_STEP linhas, de no máximo 240 px, por chamada */
		zassert_true(panel.pixel_writes - before <= DISPLAY_ROWS_PER_STEP * DISPLAY_WIDTH);
	}
	/* 28 + 21 + 14 + 21 = 84 linhas de pixels, 4 por chamada: 21 chamadas */
	zassert_equal(calls, 21U);
	zassert_equal(result, EQUIPMENT_OK);
}

/* ---------------- pedidos, cancelamento e erros ---------------- */

ZTEST(display, test_busy_invalid_arguments_and_poll_without_request)
{
	int64_t now = init_display();

	zassert_equal(display_poll(now), EQUIPMENT_FAILED); /* nada foi pedido */
	zassert_equal(display_draw_plate(NULL), -EINVAL);
	zassert_equal(display_draw_charge(NULL, 400U), -EINVAL);
	zassert_equal(display_draw_charge(PLATE, 1000001U), -EINVAL);
	zassert_equal(display_poll(now + 5), EQUIPMENT_FAILED); /* pedido inválido não cria desenho */
	zassert_ok(display_draw_plate(PLATE));
	zassert_equal(display_draw_plate(PLATE), -EBUSY);
	zassert_equal(display_draw_charge(PLATE, 400U), -EBUSY);
}

ZTEST(display, test_cancel_mid_draw_leaves_the_panel_clean_for_the_next_draw)
{
	enum equipment_result result;
	int64_t now = init_display();

	zassert_ok(display_draw_charge(PLATE, 400U));
	zassert_equal(display_poll(now + 5), EQUIPMENT_PENDING);
	zassert_equal(display_poll(now + 10), EQUIPMENT_PENDING); /* no meio de um retângulo */
	display_cancel();
	zassert_equal(display_poll(now + 15), EQUIPMENT_FAILED); /* nada em andamento */
	now += 20;
	zassert_ok(display_draw_plate(PLATE));
	poll_until_done(&now, &result);
	zassert_equal(result, EQUIPMENT_OK);
	zassert_equal(panel.errors, 0U); /* nenhum pixel pela metade nem comando fora de hora */
}

ZTEST(display, test_hardware_failures_while_drawing_fail_the_job_but_not_the_display)
{
	/* 1 CASET, 2 RASET, 3 RAMWR (abre a janela); 4 primeira linha de pixels */
	for (unsigned int call = 1U; call <= 4U; ++call) {
		enum equipment_result result;
		int64_t now;

		panel_power_on();
		now = init_display();
		fail_count = 0U;
		fail_at = call;
		zassert_ok(display_draw_plate(PLATE));
		poll_until_done(&now, &result);
		zassert_equal(result, EQUIPMENT_FAILED, "chamada %u", call);
		/* a falha é do desenho: um novo pedido ainda é aceito */
		fail_at = 0U;
		zassert_ok(display_draw_plate(PLATE));
	}
}

ZTEST(display, test_reinit_starts_over)
{
	int64_t now = init_display();

	zassert_ok(display_draw_plate(PLATE));
	zassert_ok(display_init()); /* reinicia: pedido anterior esquecido */
	zassert_equal(display_poll(now), EQUIPMENT_FAILED);
	zassert_equal(display_hw_init_fake.call_count, 2U);
}

ZTEST(display, test_ops_table_matches_the_payment_contract)
{
	const struct payment_display_ops *ops = display_ops();

	zassert_equal(ops->draw_plate, display_draw_plate);
	zassert_equal(ops->draw_charge, display_draw_charge);
	zassert_equal(ops->poll, display_poll);
	zassert_equal(ops->cancel, display_cancel);
}
