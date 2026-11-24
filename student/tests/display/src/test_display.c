/*
 * Testes do display (display.c, REQ-12) com a SPI falsa.
 */
#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "display.h"
#include "font.h"
#include "hw_fake.h"

/* Registro das transações: comando (D/C baixo) ou dados. */
#define LOG_MAX 64
static struct {
	bool data;
	uint8_t first;
	size_t len;
} wlog[LOG_MAX];
static unsigned int writes;
static unsigned int fail_at; /* 0 = nunca falha; n = falha na n-ésima escrita */

static int write_custom(bool data, uint8_t *buf, size_t len)
{
	writes++;
	if (fail_at != 0 && writes == fail_at) {
		return -EIO;
	}
	if (writes <= LOG_MAX) {
		wlog[writes - 1].data = data;
		wlog[writes - 1].first = buf[0];
		wlog[writes - 1].len = len;
	}
	return 0;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	hw_fake_reset();
	hw_display_write_fake.custom_fake = write_custom;
	writes = 0;
	fail_at = 0;
	memset(wlog, 0, sizeof(wlog));
}

ZTEST_SUITE(display, NULL, NULL, before, NULL, NULL);

ZTEST(display, test_init_sequence_and_delays)
{
	zassert_ok(display_init());

	/* Reset por hardware: ativa, depois solta. */
	zassert_equal(hw_display_reset_fake.call_count, 2);
	zassert_true(hw_display_reset_fake.arg0_history[0]);
	zassert_false(hw_display_reset_fake.arg0_history[1]);
	/* Esperas: 10 ms em reset, 120, 150 após SWRESET, 120 após SLPOUT. */
	zassert_equal(hw_sleep_ms_fake.call_count, 4);
	zassert_equal(hw_sleep_ms_fake.arg0_history[0], 10);
	zassert_equal(hw_sleep_ms_fake.arg0_history[1], 120);
	zassert_equal(hw_sleep_ms_fake.arg0_history[2], 150);
	zassert_equal(hw_sleep_ms_fake.arg0_history[3], 120);

	/* SWRESET, SLPOUT, COLMOD 55, MADCTL 00, DISPON. */
	zassert_equal(writes, 7);
	zassert_false(wlog[0].data);
	zassert_equal(wlog[0].first, 0x01);
	zassert_equal(wlog[1].first, 0x11);
	zassert_equal(wlog[2].first, 0x3A);
	zassert_true(wlog[3].data);
	zassert_equal(wlog[3].first, 0x55);
	zassert_equal(wlog[4].first, 0x36);
	zassert_equal(wlog[5].first, 0x00);
	zassert_equal(wlog[6].first, 0x29);
}

ZTEST(display, test_init_errors_are_returned)
{
	/* Cada ponto de falha devolve o erro e para a sequência. */
	for (unsigned int n = 1; n <= 7; n++) {
		before(NULL);
		fail_at = n;
		zassert_equal(display_init(), -EIO, "falha na escrita %u", n);
		zassert_equal(writes, n);
	}
}

ZTEST(display, test_init_reset_pin_errors)
{
	hw_display_reset_fake.return_val = -EIO;
	zassert_equal(display_init(), -EIO);

	before(NULL);
	hw_display_reset_fake.return_val_seq = (int[]){0, -EIO};
	hw_display_reset_fake.return_val_seq_len = 2;
	zassert_equal(display_init(), -EIO);
}

ZTEST(display, test_char_uses_valid_window_and_ramwr)
{
	zassert_ok(display_text(0, 0, "A"));
	/* CASET + 4, RASET + 4, RAMWR + pixels. */
	zassert_equal(writes, 6);
	zassert_equal(wlog[0].first, 0x2A);
	zassert_equal(wlog[1].len, 4);
	zassert_equal(wlog[2].first, 0x2B);
	zassert_equal(wlog[4].first, 0x2C);
	zassert_true(wlog[5].data);
	zassert_equal(wlog[5].len, 12 * 16 * 2, "célula 12x16 em RGB565");
}

ZTEST(display, test_text_errors_stop_drawing)
{
	for (unsigned int n = 1; n <= 6; n++) {
		before(NULL);
		fail_at = n;
		zassert_equal(display_text(0, 0, "AB"), -EIO);
	}
}

ZTEST(display, test_text_outside_screen)
{
	zassert_equal(display_text(DISPLAY_W - 5, 0, "A"), -EINVAL);
	zassert_equal(display_text(0, DISPLAY_H - 5, "A"), -EINVAL);
	zassert_equal(writes, 0);
}

ZTEST(display, test_charge_screen)
{
	zassert_ok(display_show_plate("ABC1D23"));
	zassert_equal(writes, 7 * 6, "7 células da placa");
	zassert_ok(display_show_amount(400));
	/* BRL r.cc (14 células), SIMULATED PAYMENT (17), NOT PIX (7). */
	zassert_equal(writes, (7 + 14 + 17 + 7) * 6);
}

ZTEST(display, test_long_plate_is_truncated_to_field)
{
	zassert_ok(display_show_plate("ABC1D2345"));
	zassert_equal(writes, 7 * 6);
}

ZTEST(display, test_amount_errors)
{
	/* Falha no valor, no primeiro aviso e no segundo. */
	static const unsigned int points[] = {1, 14 * 6 + 1, (14 + 17) * 6 + 1};

	for (size_t i = 0; i < ARRAY_SIZE(points); i++) {
		before(NULL);
		fail_at = points[i];
		zassert_equal(display_show_amount(400), -EIO);
	}
}

ZTEST(display, test_amount_too_large_to_format)
{
	/* "BRL 4294967295.95" não cabe no campo de 14 caracteres. */
	zassert_equal(display_show_amount(UINT32_MAX), -EINVAL);
	zassert_equal(writes, 0);
}

ZTEST(display, test_font_glyphs)
{
	static const uint8_t zero[FONT_H];

	zassert_equal(font_glyph('A')[0], 0x0E);
	zassert_equal(font_glyph('0')[0], 0x0E);
	zassert_equal(font_glyph('.')[6], 0x0C);
	zassert_mem_equal(font_glyph(' '), zero, FONT_H);
	zassert_mem_equal(font_glyph('a'), zero, FONT_H, "sem glifo: branco");
}
