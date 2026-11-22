/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file st7789.c
 * @brief Subconjunto nativo do ST7789V: reset por hardware, init, CASET/RASET/RAMWR.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

#include "font.h"
#include "st7789.h"

/* Comandos do subconjunto aceito pelo modelo. */
#define CMD_SWRESET	0x01
#define CMD_SLPOUT	0x11
#define CMD_DISPON	0x29
#define CMD_CASET	0x2a
#define CMD_RASET	0x2b
#define CMD_RAMWR	0x2c
#define CMD_COLMOD	0x3a
#define CMD_MADCTL	0x36

#define COR_FUNDO	0x0000
#define COR_TEXTO	0xffff

#define DISPLAY_STACK	2048
#define DISPLAY_PRIO	7
#define FILA_DISPLAY	4

static const struct spi_dt_spec display =
	SPI_DT_SPEC_GET(DT_ALIAS(payment_display),
			SPI_OP_MODE_CONTROLLER | SPI_WORD_SET(8));
static const struct gpio_dt_spec dc =
	GPIO_DT_SPEC_GET(DT_ALIAS(payment_display), dc_gpios);
static const struct gpio_dt_spec reset =
	GPIO_DT_SPEC_GET(DT_ALIAS(payment_display), reset_gpios);

enum pedido_tipo {
	PEDIDO_PLACA,
	PEDIDO_COBRANCA,
};

struct pedido {
	enum pedido_tipo tipo;
	char plate[8];
	uint32_t cents;
};

K_THREAD_STACK_DEFINE(disp_stack, DISPLAY_STACK);

static struct {
	struct k_msgq fila;
	char fila_mem[FILA_DISPLAY * sizeof(struct pedido)];
	struct k_thread thread;
	uint8_t linha[ST7789_WIDTH * 2];
	bool inicializado;
} disp;

static int escreve(const uint8_t *dados, size_t len)
{
	struct spi_buf buf = { .buf = (void *)dados, .len = len };
	struct spi_buf_set set = { .buffers = &buf, .count = 1 };

	return spi_write_dt(&display, &set);
}

static int comando(uint8_t cmd, const uint8_t *dados, size_t len)
{
	int rc;

	gpio_pin_set_dt(&dc, 0);
	rc = escreve(&cmd, 1);
	gpio_pin_set_dt(&dc, 1);
	if (rc == 0 && dados != NULL && len > 0) {
		rc = escreve(dados, len);
	}
	return rc;
}

static int janela(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	uint8_t caset[4];
	uint8_t raset[4];
	int rc;

	if (w == 0 || h == 0 || (x + w) > ST7789_WIDTH || (y + h) > ST7789_HEIGHT) {
		return -EINVAL;
	}

	caset[0] = (uint8_t)(x >> 8);
	caset[1] = (uint8_t)x;
	caset[2] = (uint8_t)((x + w - 1) >> 8);
	caset[3] = (uint8_t)(x + w - 1);
	rc = comando(CMD_CASET, caset, sizeof(caset));
	if (rc != 0) {
		return rc;
	}

	raset[0] = (uint8_t)(y >> 8);
	raset[1] = (uint8_t)y;
	raset[2] = (uint8_t)((y + h - 1) >> 8);
	raset[3] = (uint8_t)(y + h - 1);
	rc = comando(CMD_RASET, raset, sizeof(raset));
	if (rc != 0) {
		return rc;
	}

	return comando(CMD_RAMWR, NULL, 0);
}

static uint16_t largura_texto(const char *s, uint8_t escala)
{
	return (uint16_t)(strlen(s) * FONT_ADVANCE * escala);
}

static int texto(uint16_t x, uint16_t y, const char *s, uint8_t escala)
{
	uint16_t w = largura_texto(s, escala);
	uint16_t h = (uint16_t)(FONT_ROWS * escala);
	size_t n = strlen(s);
	int rc;

	rc = janela(x, y, w, h);
	if (rc != 0) {
		return rc;
	}

	/* Uma linha de texto, repetida escala vezes para engordar o glifo. */
	for (int linha = 0; linha < FONT_ROWS; linha++) {
		size_t px = 0;

		for (size_t i = 0; i < n; i++) {
			const uint8_t *g = font_glyph(s[i]);

			for (int col = 0; col < FONT_COLS; col++) {
				bool aceso = g != NULL && ((g[col] >> linha) & 1) != 0;

				for (int r = 0; r < escala; r++) {
					uint16_t cor = aceso ? COR_TEXTO : COR_FUNDO;

					disp.linha[px * 2] = (uint8_t)(cor >> 8);
					disp.linha[px * 2 + 1] = (uint8_t)cor;
					px++;
				}
			}
			for (int r = 0; r < escala; r++) {
				disp.linha[px * 2] = 0;
				disp.linha[px * 2 + 1] = 0;
				px++;
			}
		}

		for (int r = 0; r < escala; r++) {
			if (escreve(disp.linha, px * 2) != 0) {
				return -EIO;
			}
		}
	}
	return 0;
}

static uint16_t centralizado(const char *s, uint8_t escala)
{
	uint16_t w = largura_texto(s, escala);

	return (uint16_t)((ST7789_WIDTH - w) / 2);
}

static int inicializa(void)
{
	static const uint8_t colmod = 0x55;
	static const uint8_t madctl = 0x00;
	int rc;

	if (!spi_is_ready_dt(&display) || !gpio_is_ready_dt(&dc) ||
	    !gpio_is_ready_dt(&reset)) {
		return -ENODEV;
	}

	/* Reset por hardware: 10 ms em nivel baixo, 120 ms de recuperacao. */
	gpio_pin_set_dt(&reset, 0);
	k_msleep(10);
	gpio_pin_set_dt(&reset, 1);
	k_msleep(120);

	rc = comando(CMD_SWRESET, NULL, 0);
	if (rc != 0) {
		return rc;
	}
	k_msleep(150);

	rc = comando(CMD_SLPOUT, NULL, 0);
	if (rc != 0) {
		return rc;
	}
	k_msleep(120);

	rc = comando(CMD_COLMOD, &colmod, 1);
	if (rc == 0) {
		rc = comando(CMD_MADCTL, &madctl, 1);
	}
	if (rc == 0) {
		rc = comando(CMD_DISPON, NULL, 0);
	}
	return rc;
}

static void desenha_placa(const char *plate)
{
	uint8_t escala = 4;
	uint16_t x = centralizado(plate, escala);

	(void)texto(x, 40, plate, escala);
}

static void desenha_cobranca(uint32_t cents)
{
	char valor[12];

	/* "BRL r.cc": parte inteira e duas casas, sempre com dois digitos. */
	(void)snprintf(valor, sizeof(valor), "BRL %u.%02u",
		       (unsigned int)(cents / 100U), (unsigned int)(cents % 100U));

	(void)texto(centralizado(valor, 3), 120, valor, 3);
	(void)texto(centralizado("SIMULATED PAYMENT", 2), 200, "SIMULATED PAYMENT", 2);
	(void)texto(centralizado("NOT PIX", 2), 240, "NOT PIX", 2);
}

static void thread_display(void *a, void *b, void *c)
{
	struct pedido pedido;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		k_msgq_get(&disp.fila, &pedido, K_FOREVER);

		if (!disp.inicializado) {
			if (inicializa() != 0) {
				continue;
			}
			disp.inicializado = true;
		}

		if (pedido.tipo == PEDIDO_PLACA) {
			desenha_placa(pedido.plate);
		} else {
			desenha_cobranca(pedido.cents);
		}
	}
}

int st7789_start(void)
{
	k_msgq_init(&disp.fila, disp.fila_mem, sizeof(struct pedido), FILA_DISPLAY);
	k_thread_create(&disp.thread, disp_stack, K_THREAD_STACK_SIZEOF(disp_stack),
			thread_display, NULL, NULL, NULL, DISPLAY_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&disp.thread, "display");
	return 0;
}

int st7789_show_plate(const char *plate)
{
	struct pedido pedido = { .tipo = PEDIDO_PLACA };

	if (plate == NULL || strlen(plate) != 7) {
		return -EINVAL;
	}
	memcpy(pedido.plate, plate, 8);
	return k_msgq_put(&disp.fila, &pedido, K_NO_WAIT) == 0 ? 0 : -ENOMSG;
}

int st7789_show_charge(uint32_t cents)
{
	struct pedido pedido = { .tipo = PEDIDO_COBRANCA, .cents = cents };

	return k_msgq_put(&disp.fila, &pedido, K_NO_WAIT) == 0 ? 0 : -ENOMSG;
}
