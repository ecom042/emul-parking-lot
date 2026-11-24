/*
 * Display: sequência de inicialização, tela de cobrança e erros de SPI.
 * hw_disp_write é falso: registramos os comandos (D/C baixo) enviados.
 */
#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "display.h"
#include "fakes.h"
#include "font.h"

/* Comandos enviados (bytes com D/C baixo), em ordem. */
static uint8_t cmds[64];
static size_t ncmds;
/* Falha na escrita número fail_at (1 = primeira); 0 = nunca falha. */
static int fail_at;
static int writes;
/* Algum pixel aceso (diferente de preto) foi enviado? */
static bool lit;

static int write_fake(bool data, uint8_t *buf, size_t len)
{
	writes++;
	if (fail_at != 0 && writes == fail_at) {
		return -EIO;
	}
	if (!data && ncmds < sizeof(cmds)) {
		cmds[ncmds++] = buf[0];
	}
	/* Só as linhas de pixels (240 x 2 bytes), não os parâmetros. */
	for (size_t i = 0; data && len == DISPLAY_WIDTH * 2 && i < len; i++) {
		lit = lit || buf[i] != 0;
	}
	return 0;
}

static void before(void *f)
{
	ARG_UNUSED(f);
	fakes_reset();
	hw_disp_write_fake.custom_fake = write_fake;
	ncmds = 0;
	fail_at = 0;
	writes = 0;
	lit = false;
}

ZTEST_SUITE(display, NULL, NULL, before, NULL, NULL);

ZTEST(display, test_inicializacao)
{
	static const uint8_t expected[] = {ST7789_SWRESET, ST7789_SLPOUT, ST7789_COLMOD,
					   ST7789_MADCTL,  ST7789_DISPON, ST7789_CASET,
					   ST7789_RASET,   ST7789_RAMWR};

	zassert_ok(display_init());
	/* Reset por hardware: ativo, depois solto. */
	zassert_equal(hw_disp_reset_fake.call_count, 2);
	zassert_true(hw_disp_reset_fake.arg0_history[0]);
	zassert_false(hw_disp_reset_fake.arg0_history[1]);
	/* Esperas da folha de dados: 10, 120, 150 e 120 ms. */
	zassert_equal(hw_sleep_ms_fake.call_count, 4);
	zassert_equal(hw_sleep_ms_fake.arg0_history[2], 150);
	zassert_equal(ncmds, sizeof(expected));
	zassert_mem_equal(cmds, expected, sizeof(expected));
	zassert_false(lit, "tela inicial preta");
}

ZTEST(display, test_inicializacao_falha_no_reset)
{
	hw_disp_reset_fake.return_val = -EIO;
	zassert_equal(display_init(), -EIO);
	zassert_equal(hw_disp_write_fake.call_count, 0, "nada depois da falha");
}

static int reset_fake(bool asserted)
{
	return asserted ? 0 : -EIO;
}

ZTEST(display, test_inicializacao_falha_ao_soltar_reset)
{
	hw_disp_reset_fake.custom_fake = reset_fake;
	zassert_equal(display_init(), -EIO);
	zassert_equal(hw_disp_write_fake.call_count, 0);
}

ZTEST(display, test_inicializacao_falha_em_cada_escrita)
{
	/*
	 * 12 escritas de comando/parâmetro (SWRESET, SLPOUT, COLMOD+1,
	 * MADCTL+1, DISPON, CASET+1, RASET+1, RAMWR) e então os pixels.
	 */
	for (int n = 1; n <= 13; n++) {
		before(NULL);
		fail_at = n;
		zassert_equal(display_init(), -EIO, "falha na escrita %d", n);
		zassert_equal(writes, n, "parou na primeira falha (%d)", n);
	}
}

ZTEST(display, test_tela_de_cobranca)
{
	zassert_ok(display_show_plate("ABC1D23"));
	zassert_true(lit);
	/* 4 faixas, cada uma com CASET, RASET, RAMWR. */
	zassert_equal(ncmds, 12);
	ncmds = 0;
	zassert_ok(display_show_amount(400));
	zassert_equal(ncmds, 9);
}

ZTEST(display, test_tela_de_cobranca_falhas)
{
	/*
	 * Cada faixa: 5 escritas de janela + uma por linha de pixels (24 na
	 * escala 3, 16 na escala 2). Falha na primeira escrita de cada faixa.
	 */
	static const int band_start[] = {1, 30, 59, 80};

	for (int n = 0; n < 4; n++) {
		before(NULL);
		fail_at = band_start[n];
		zassert_equal(display_show_plate("ABC1D23"), -EIO, "faixa %d", n);
		zassert_equal(writes, band_start[n]);
	}
	for (int n = 0; n < 3; n++) {
		before(NULL);
		fail_at = band_start[n];
		zassert_equal(display_show_amount(400), -EIO, "faixa %d", n);
		zassert_equal(writes, band_start[n]);
	}
}

ZTEST(display, test_janela_falha_em_raset_e_ramwr)
{
	fail_at = 3; /* CASET, parâmetros, RASET */
	zassert_equal(display_text_band(0, 2, "A", COLOR_WHITE), -EIO);
	before(NULL);
	fail_at = 5; /* ..., parâmetros do RASET, RAMWR */
	zassert_equal(display_text_band(0, 2, "A", COLOR_WHITE), -EIO);
}

ZTEST(display, test_texto_longo_e_cortado)
{
	/* 40 caracteres em escala 2 não cabem em 240 px: corta sem estourar. */
	zassert_ok(display_text_band(0, 2, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", COLOR_WHITE));
	zassert_true(lit);
}

ZTEST(display, test_formata_valor)
{
	char buf[16];

	zassert_equal(display_format_amount(buf, sizeof(buf), 400), 8);
	zassert_str_equal(buf, "BRL 4.00");
	zassert_true(display_format_amount(buf, sizeof(buf), 1000000) > 0);
	zassert_str_equal(buf, "BRL 10000.00");
	display_format_amount(buf, sizeof(buf), 205);
	zassert_str_equal(buf, "BRL 2.05");
	zassert_equal(display_format_amount(buf, 4, 400), -1);
}

ZTEST(display, test_fonte)
{
	static const uint8_t zero[FONT_HEIGHT] = {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e};
	const uint8_t *blank = font_glyph(' ');

	zassert_mem_equal(font_glyph('0'), zero, FONT_HEIGHT);
	zassert_equal(font_glyph('.')[6], 0x0c);
	zassert_equal(font_glyph('Z')[0], 0x1f);
	zassert_equal(font_glyph('?'), blank, "desconhecido vira espaço");
	zassert_equal(font_glyph('a'), blank);
	zassert_equal(font_glyph('/'), blank);
	zassert_equal(font_glyph(':'), blank);
	zassert_equal(font_glyph('@'), blank);
	zassert_equal(font_glyph('['), blank);
}
