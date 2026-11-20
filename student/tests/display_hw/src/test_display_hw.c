/*
 * test_display_hw.c — teste do driver REAL do display (SPI1 + GPIOs D/C e reset), sem falsos.
 *
 * Roda a sequência completa do contrato pelas funções de display_hw.c e confere que
 * cada operação do driver Zephyr TERMINA com sucesso (retorno 0): o reset por
 * hardware, a inicialização e uma janela de 4 pixels. O que o modelo ST7789V
 * "viu" é verificado pelos cenários do Renode; este teste prova que a fiação
 * SPI/GPIO/devicetree funciona.
 */
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "display_hw.h"

ZTEST_SUITE(display_spi_hw, NULL, NULL, NULL, NULL, NULL);

ZTEST(display_spi_hw, test_init_finds_spi_and_gpios)
{
	zassert_ok(display_hw_init());
}

ZTEST(display_spi_hw, test_full_init_and_a_small_window_through_the_real_spi)
{
	static const uint8_t rgb565 = 0x55U;
	static const uint8_t native = 0x00U;
	static const uint8_t columns[4] = { 0x00U, 0x00U, 0x00U, 0x03U }; /* x = 0..3 */
	static const uint8_t rows[4] = { 0x00U, 0x00U, 0x00U, 0x00U };    /* y = 0 */
	static const uint8_t pixels[8] = { 0xF8U, 0x00U, 0x07U, 0xE0U, 0x00U, 0x1FU, 0xFFU, 0xFFU };

	zassert_ok(display_hw_init());
	zassert_ok(display_hw_reset(true)); /* borda de descida: o modelo exige */
	k_msleep(10);
	zassert_ok(display_hw_reset(false));
	k_msleep(120);
	zassert_ok(display_hw_command(0x01U, NULL, 0U)); /* SWRESET */
	k_msleep(150);
	zassert_ok(display_hw_command(0x11U, NULL, 0U)); /* SLPOUT */
	k_msleep(120);
	zassert_ok(display_hw_command(0x3AU, &rgb565, 1U)); /* COLMOD */
	zassert_ok(display_hw_command(0x36U, &native, 1U)); /* MADCTL */
	zassert_ok(display_hw_command(0x29U, NULL, 0U));    /* DISPON */
	zassert_ok(display_hw_command(0x2AU, columns, sizeof(columns))); /* CASET */
	zassert_ok(display_hw_command(0x2BU, rows, sizeof(rows)));       /* RASET */
	zassert_ok(display_hw_command(0x2CU, NULL, 0U));                 /* RAMWR */
	zassert_ok(display_hw_data(pixels, sizeof(pixels)));
}

ZTEST(display_spi_hw, test_invalid_arguments_are_rejected)
{
	zassert_equal(display_hw_data(NULL, 4U), -EINVAL);
	zassert_equal(display_hw_command(0x2AU, NULL, 4U), -EINVAL);
}
