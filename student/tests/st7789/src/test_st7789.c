/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Display de pagamento (st7789.c) com SPI e pinos falsos (FFF). A thread do
 * display e a real: os testes enfileiram pedidos, dormem para ela trabalhar e
 * conferem o que os falsos registraram no barramento (veja fakes.h) e os
 * numeros publicados por st7789_done() e st7789_failed().
 *
 * A thread e o estado do modulo sao estaticos: por isso os testes tem numero
 * no nome (o ztest os roda em ordem alfabetica). O 00 cria a thread com o SPI
 * fora do ar, entao a inicializacao do boot falha; de 02 a 13 cada pedido
 * tenta de novo e falha de um jeito; o 14 inicializa; dali em diante so se
 * desenha. O ultimo teste recria a thread com os drivers prontos para ver a
 * inicializacao do boot dar certo.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "fakes.h"
#include "font.h"
#include "st7789.h"

#define CMD_SWRESET 0x01
#define CMD_SLPOUT  0x11
#define CMD_DISPON  0x29
#define CMD_CASET   0x2a
#define CMD_RASET   0x2b
#define CMD_RAMWR   0x2c
#define CMD_COLMOD  0x3a
#define CMD_MADCTL  0x36

#define FILA 4 /* FILA_DISPLAY em st7789.c */

/* Escritas SPI da inicializacao: SWRESET, SLPOUT, COLMOD + 0x55,
 * MADCTL + 0x00, DISPON.
 */
#define ESCRITAS_INIT 7

/* Escritas de um texto: CASET + dados, RASET + dados, RAMWR, e uma por linha
 * de pixels (7 linhas do glifo, cada uma repetida escala vezes).
 */
#define ESCRITAS_TEXTO(escala) (5 + FONT_ROWS * (escala))

/* Ultimo numero de pedido devolvido pelo modulo e ultimo que falhou. */
static uint32_t emitidos;
static uint32_t ultima_falha;

static void preparar(void *fixture)
{
	ARG_UNUSED(fixture);
	fakes_reset();
}

ZTEST_SUITE(st7789, NULL, NULL, preparar, NULL, NULL);

/* ---- Auxiliares --------------------------------------------------------- */

/* A inicializacao dorme 10 + 120 + 150 + 120 = 400 ms. */
static void esperar_init(void)
{
	k_sleep(K_MSEC(600));
}

/* Desenhar nao dorme: basta a thread do display rodar. */
static void esperar_desenho(void)
{
	k_sleep(K_MSEC(100));
}

static void confere_cmd(int i, uint8_t cmd, uint8_t n)
{
	zassert_true(i < bus.n_cmds, "comando %d nao enviado (so %d)", i, bus.n_cmds);
	zassert_equal(bus.cmds[i].cmd, cmd, "comando %d: 0x%02x, esperado 0x%02x", i,
		      bus.cmds[i].cmd, cmd);
	zassert_equal(bus.cmds[i].n, n, "comando 0x%02x com %u bytes, esperado %u", cmd,
		      bus.cmds[i].n, n);
}

static void confere_coords(int i, uint8_t cmd, uint16_t ini, uint16_t fim)
{
	const uint8_t *d = bus.cmds[i].dados;

	confere_cmd(i, cmd, 4);
	zassert_equal((d[0] << 8) | d[1], ini, "0x%02x comeca em %u, esperado %u", cmd,
		      (d[0] << 8) | d[1], ini);
	zassert_equal((d[2] << 8) | d[3], fim, "0x%02x termina em %u, esperado %u", cmd,
		      (d[2] << 8) | d[3], fim);
}

/* Sequencia de inicializacao completa nos 5 primeiros comandos. */
static void confere_init(void)
{
	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 2);
	zassert_true(pino_e(fake_gpio_pin_configure_dt_fake.arg0_history[0], PINO_DC));
	zassert_true(pino_e(fake_gpio_pin_configure_dt_fake.arg0_history[1], PINO_RESET));
	zassert_equal(fake_gpio_pin_configure_dt_fake.arg1_history[0], GPIO_OUTPUT_INACTIVE);
	zassert_equal(fake_gpio_pin_configure_dt_fake.arg1_history[1], GPIO_OUTPUT_INACTIVE);

	/* Reset ativo em baixo: 1 prende, 0 solta. */
	zassert_equal(bus.n_reset, 2);
	zassert_equal(bus.reset_nivel[0], 1);
	zassert_equal(bus.reset_nivel[1], 0);

	confere_cmd(0, CMD_SWRESET, 0);
	confere_cmd(1, CMD_SLPOUT, 0);
	confere_cmd(2, CMD_COLMOD, 1);
	zassert_equal(bus.cmds[2].dados[0], 0x55, "COLMOD deve pedir RGB565");
	confere_cmd(3, CMD_MADCTL, 1);
	zassert_equal(bus.cmds[3].dados[0], 0x00);
	confere_cmd(4, CMD_DISPON, 0);
}

/* Pixels acesos que o texto deve ter: bits do glifo vezes escala ao quadrado. */
static uint32_t acesos_esperados(const char *s, uint8_t escala)
{
	uint32_t n = 0;

	for (size_t i = 0; s[i] != '\0'; i++) {
		const uint8_t *g = font_glyph(s[i]);

		for (int col = 0; g != NULL && col < FONT_COLS; col++) {
			for (int linha = 0; linha < FONT_ROWS; linha++) {
				n += (g[col] >> linha) & 1U;
			}
		}
	}
	return n * escala * escala;
}

/* Le o texto desenhado na janela comparando cada celula com a fonte.
 * Celula apagada vira espaco; desenho que nao e glifo vira '#'.
 */
static void decodifica(const struct janela_reg *j, char *out, size_t tam)
{
	static const char tabela[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-";
	size_t n = (size_t)(j->x1 - j->x0 + 1) / (FONT_ADVANCE * j->escala);
	size_t i;

	for (i = 0; i < n && i + 1 < tam; i++) {
		const uint8_t *cel = &j->col[i * FONT_ADVANCE];

		out[i] = '#';
		if (cel[FONT_COLS] != 0) {
			continue; /* a coluna de espacamento tem que ficar apagada */
		}
		for (size_t k = 0; k < sizeof(tabela) - 1; k++) {
			if (memcmp(cel, font_glyph(tabela[k]), FONT_COLS) == 0) {
				out[i] = tabela[k];
				break;
			}
		}
	}
	out[i] = '\0';
}

/* Confere a janela @p jan e os comandos que a abriram (a partir de @p cmd0):
 * @p s centralizado na linha @p y com a escala dada, lido como @p visto.
 */
static void confere_texto(int jan, int cmd0, const char *s, const char *visto,
			  uint8_t escala, uint16_t y)
{
	uint16_t w = (uint16_t)(strlen(s) * FONT_ADVANCE * escala);
	uint16_t h = (uint16_t)(FONT_ROWS * escala);
	uint16_t x = (uint16_t)((ST7789_WIDTH - w) / 2);
	const struct janela_reg *j;
	char lido[32];

	confere_coords(cmd0, CMD_CASET, x, x + w - 1);
	confere_coords(cmd0 + 1, CMD_RASET, y, y + h - 1);
	confere_cmd(cmd0 + 2, CMD_RAMWR, 0);

	zassert_true(jan < bus.n_jan, "janela %d nao aberta", jan);
	j = &bus.jan[jan];
	zassert_equal(j->escala, escala);
	zassert_equal(j->bytes, (uint32_t)w * h * 2, "'%s': %u bytes, esperado %u", s,
		      j->bytes, (uint32_t)w * h * 2);
	zassert_equal(j->excesso, 0);
	zassert_equal(j->outros, 0, "so branco e preto");
	zassert_equal(j->acesos, acesos_esperados(s, escala), "'%s': %u acesos, esperado %u",
		      s, j->acesos, acesos_esperados(s, escala));
	decodifica(j, lido, sizeof(lido));
	zassert_str_equal(lido, visto, "lido '%s', esperado '%s'", lido, visto);
}

/* Os tres textos da cobranca a partir da janela @p jan e do comando @p cmd0. */
static void confere_cobranca(int jan, int cmd0, const char *valor)
{
	confere_texto(jan, cmd0, valor, valor, 3, 120);
	confere_texto(jan + 1, cmd0 + 3, "SIMULATED PAYMENT", "SIMULATED PAYMENT", 2, 200);
	confere_texto(jan + 2, cmd0 + 6, "NOT PIX", "NOT PIX", 2, 240);
}

/* Nada de lixo no barramento. */
static void confere_barramento_limpo(void)
{
	zassert_equal(bus.dados_soltos, 0);
	zassert_equal(bus.estouro, 0);
	zassert_equal(bus.spec_errado, 0);
}

/* Enfileira a placa e confere que o numero e o seguinte ao ultimo. */
static uint32_t pede_placa(const char *plate)
{
	uint32_t t = 0;

	zassert_equal(st7789_show_plate(plate, &t), 0, "placa '%s' recusada", plate);
	zassert_equal(t, emitidos + 1U, "numero %u, esperado %u", t, emitidos + 1U);
	emitidos = t;
	return t;
}

/* Enfileira a cobranca e confere que o numero e o seguinte ao ultimo. */
static uint32_t pede_cobranca(uint32_t cents)
{
	uint32_t t = 0;

	zassert_equal(st7789_show_charge(cents, &t), 0, "%u centavos recusados", cents);
	zassert_equal(t, emitidos + 1U, "numero %u, esperado %u", t, emitidos + 1U);
	emitidos = t;
	return t;
}

/* O pedido @p t terminou, com falha ou desenhado. Uma falha publica o numero
 * em st7789_failed(); um pedido desenhado nao mexe na ultima falha.
 */
static void confere_fim(uint32_t t, bool falhou)
{
	if (falhou) {
		ultima_falha = t;
	}
	zassert_equal(st7789_done(), t, "concluido %u, esperado %u", st7789_done(), t);
	zassert_equal(st7789_failed(), ultima_falha, "falho %u, esperado %u",
		      st7789_failed(), ultima_falha);
}

/* ---- Boot sem SPI, pedidos recusados e inicializacao que falha ---------- */

ZTEST(st7789, test_00_boot_sem_spi_pronto_nao_inicializa)
{
	spi_pronto(false);
	zassert_equal(st7789_start(), 0);
	/* O teste roda numa thread cooperativa: a do display so comeca no sleep. */
	zassert_equal(fake_spi_is_ready_dt_fake.call_count, 0);

	esperar_desenho();
	/* A thread tentou inicializar sozinha, sem pedido nenhum, e desistiu
	 * antes de tocar nos pinos.
	 */
	zassert_equal(fake_spi_is_ready_dt_fake.call_count, 1);
	zassert_equal(fake_gpio_is_ready_dt_fake.call_count, 0);
	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 0);
	zassert_equal(bus.n_reset, 0);
	zassert_equal(bus.escritas, 0);
	/* A falha do boot nao e de pedido nenhum. */
	zassert_equal(st7789_done(), 0);
	zassert_equal(st7789_failed(), 0);
	confere_barramento_limpo();
}

ZTEST(st7789, test_01_pedido_invalido_e_recusado_sem_gastar_numero)
{
	uint32_t t = 0xdeadU;

	zassert_equal(st7789_show_plate(NULL, &t), -EINVAL);
	zassert_equal(st7789_show_plate("", &t), -EINVAL);
	zassert_equal(st7789_show_plate("ABC123", &t), -EINVAL);
	zassert_equal(st7789_show_plate("ABCD1234", &t), -EINVAL);
	zassert_equal(st7789_show_plate("ABC1D23", NULL), -EINVAL);
	zassert_equal(st7789_show_charge(400, NULL), -EINVAL);
	zassert_equal(t, 0xdeadU, "pedido recusado nao recebe numero");
	esperar_desenho();

	/* Nada chegou a thread: ela nem tentou inicializar de novo. */
	zassert_equal(fake_spi_is_ready_dt_fake.call_count, 0);
	zassert_equal(bus.escritas, 0);
	zassert_equal(st7789_done(), 0);
	zassert_equal(st7789_failed(), 0);
	/* O primeiro pedido aceito (no proximo teste) ainda recebe o 1. */
}

ZTEST(st7789, test_02_fila_cheia_nao_gasta_numero_e_cada_pedido_tenta_inicializar)
{
	uint32_t t = 0xdeadU;
	uint32_t c;

	spi_pronto(false);

	/* A thread ja espera na fila: o primeiro pedido vai direto para ela e
	 * os FILA seguintes ocupam a fila (numeros 1 a FILA + 1).
	 */
	for (int i = 0; i < FILA + 1; i++) {
		(void)pede_placa("ABC1D23");
	}
	zassert_equal(emitidos, FILA + 1);
	zassert_equal(st7789_show_plate("ABC1D23", &t), -ENOMSG);
	zassert_equal(st7789_show_charge(400, &t), -ENOMSG);
	zassert_equal(t, 0xdeadU, "pedido recusado nao recebe numero");
	zassert_equal(st7789_done(), 0, "nada termina antes de a thread rodar");

	esperar_init();
	/* Cada pedido tentou inicializar, desistiu antes dos pinos e falhou. */
	zassert_equal(fake_spi_is_ready_dt_fake.call_count, FILA + 1);
	zassert_equal(fake_gpio_is_ready_dt_fake.call_count, 0);
	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 0);
	zassert_equal(bus.n_reset, 0);
	zassert_equal(bus.escritas, 0);
	confere_fim(emitidos, true);

	/* A fila esvaziou: a cobranca volta a ser aceita, com o numero seguinte
	 * ao ultimo aceito (as recusas nao gastaram numero), e tenta de novo.
	 */
	c = pede_cobranca(400);
	zassert_equal(c, FILA + 2);
	esperar_init();
	zassert_equal(fake_spi_is_ready_dt_fake.call_count, FILA + 2);
	zassert_equal(bus.escritas, 0);
	confere_fim(c, true);
	confere_barramento_limpo();
}

ZTEST(st7789, test_03_pino_dc_nao_pronto)
{
	uint32_t t;

	pino_pronto(PINO_DC, false);
	t = pede_placa("ABC1D23");
	esperar_init();

	zassert_equal(fake_spi_is_ready_dt_fake.call_count, 1);
	zassert_equal(fake_gpio_is_ready_dt_fake.call_count, 1);
	zassert_true(pino_e(fake_gpio_is_ready_dt_fake.arg0_history[0], PINO_DC));
	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 0);
	zassert_equal(bus.escritas, 0);
	confere_fim(t, true);
	confere_barramento_limpo();
}

ZTEST(st7789, test_04_pino_reset_nao_pronto)
{
	uint32_t t;

	pino_pronto(PINO_RESET, false);
	t = pede_placa("ABC1D23");
	esperar_init();

	zassert_equal(fake_gpio_is_ready_dt_fake.call_count, 2);
	zassert_true(pino_e(fake_gpio_is_ready_dt_fake.arg0_history[0], PINO_DC));
	zassert_true(pino_e(fake_gpio_is_ready_dt_fake.arg0_history[1], PINO_RESET));
	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 0);
	zassert_equal(bus.escritas, 0);
	confere_fim(t, true);
	confere_barramento_limpo();
}

ZTEST(st7789, test_05_erro_ao_configurar_dc)
{
	uint32_t t;

	pino_erro_config(PINO_DC, -EIO);
	t = pede_placa("ABC1D23");
	esperar_init();

	/* Desiste no primeiro pino: o reset nem e configurado nem pulsado. */
	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 1);
	zassert_true(pino_e(fake_gpio_pin_configure_dt_fake.arg0_history[0], PINO_DC));
	zassert_equal(fake_gpio_pin_set_dt_fake.call_count, 0);
	zassert_equal(bus.escritas, 0);
	confere_fim(t, true);
	confere_barramento_limpo();
}

ZTEST(st7789, test_06_erro_ao_configurar_reset)
{
	uint32_t t;

	pino_erro_config(PINO_RESET, -EIO);
	t = pede_cobranca(400);
	esperar_init();

	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 2);
	zassert_true(pino_e(fake_gpio_pin_configure_dt_fake.arg0_history[1], PINO_RESET));
	zassert_equal(fake_gpio_pin_set_dt_fake.call_count, 0);
	zassert_equal(bus.escritas, 0);
	confere_fim(t, true);
	confere_barramento_limpo();
}

/* Faz a escrita @p n da inicializacao falhar e confere que ela parou ali: os
 * comandos antes dela foram enviados, a placa nao foi desenhada e o pedido
 * terminou com falha.
 */
static void falha_na_init(int n, int cmds_enviados)
{
	uint32_t t;

	spi_falha_na_escrita(n, -EIO);
	t = pede_placa("ABC1D23");
	esperar_init();

	zassert_equal(bus.n_reset, 2, "o reset por hardware vem antes do SPI");
	zassert_equal(bus.escritas, n, "%d escritas, esperado %d", bus.escritas, n);
	zassert_equal(bus.n_cmds, cmds_enviados);
	zassert_equal(bus.n_jan, 0, "nada pode ser desenhado");
	confere_fim(t, true);
	confere_barramento_limpo();
}

ZTEST(st7789, test_07_erro_spi_no_swreset)
{
	falha_na_init(1, 0);
}

ZTEST(st7789, test_08_erro_spi_no_slpout)
{
	falha_na_init(2, 1);
	confere_cmd(0, CMD_SWRESET, 0);
}

ZTEST(st7789, test_09_erro_spi_no_comando_colmod)
{
	falha_na_init(3, 2);
	confere_cmd(1, CMD_SLPOUT, 0);
}

ZTEST(st7789, test_10_erro_spi_no_dado_do_colmod)
{
	/* O comando passou e o parametro falhou: MADCTL nao pode vir depois. */
	falha_na_init(4, 3);
	confere_cmd(2, CMD_COLMOD, 0);
}

ZTEST(st7789, test_11_erro_spi_no_comando_madctl)
{
	falha_na_init(5, 3);
	confere_cmd(2, CMD_COLMOD, 1);
}

ZTEST(st7789, test_12_erro_spi_no_dado_do_madctl)
{
	falha_na_init(6, 4);
	confere_cmd(3, CMD_MADCTL, 0);
}

ZTEST(st7789, test_13_erro_spi_no_dispon)
{
	falha_na_init(7, 4);
	confere_cmd(3, CMD_MADCTL, 1);
}

/* ---- Inicializacao no pedido e desenho --------------------------------- */

ZTEST(st7789, test_14_inicializa_no_pedido_seguinte_e_desenha_a_placa)
{
	uint32_t t = pede_placa("ABC1D23");

	/* Reset preso 10 ms e solto; aos 50 ms ainda corre a recuperacao de
	 * 120 ms: nenhum comando saiu e o pedido nao terminou.
	 */
	k_sleep(K_MSEC(50));
	zassert_equal(bus.n_reset, 2);
	zassert_equal(bus.escritas, 0, "comando durante a recuperacao do reset");
	zassert_equal(st7789_done(), t - 1U, "pedido concluido durante a inicializacao");

	esperar_init();
	/* Depois de todas as falhas a inicializacao recomeca do zero. */
	confere_init();
	zassert_equal(bus.n_cmds, 5 + 3);
	zassert_equal(bus.n_jan, 1);
	confere_texto(0, 5, "ABC1D23", "ABC1D23", 4, 40);
	zassert_equal(bus.escritas, ESCRITAS_INIT + ESCRITAS_TEXTO(4));
	/* Desenhado: conclui sem mexer na ultima falha (a do teste anterior). */
	confere_fim(t, false);
	zassert_equal(st7789_failed(), t - 1U);
	confere_barramento_limpo();
}

ZTEST(st7789, test_15_ja_inicializado_nao_inicializa_de_novo)
{
	uint32_t t = pede_placa("XYZ9876");

	esperar_desenho();

	zassert_equal(fake_spi_is_ready_dt_fake.call_count, 0);
	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 0);
	zassert_equal(bus.n_reset, 0);
	zassert_equal(bus.n_cmds, 3);
	confere_texto(0, 0, "XYZ9876", "XYZ9876", 4, 40);
	zassert_equal(bus.escritas, ESCRITAS_TEXTO(4));
	confere_fim(t, false);
	confere_barramento_limpo();
}

ZTEST(st7789, test_16_caractere_fora_da_fonte_fica_apagado)
{
	/* '?' e 'q' nao estao na fonte: a celula sai apagada, como um espaco. */
	uint32_t t = pede_placa("A?-1.q2");

	esperar_desenho();

	confere_texto(0, 0, "A?-1.q2", "A -1. 2", 4, 40);
	confere_fim(t, false);
	confere_barramento_limpo();
}

ZTEST(st7789, test_17_cobranca_em_reais_com_centavos)
{
	uint32_t t = pede_cobranca(400);

	esperar_desenho();

	zassert_equal(bus.n_cmds, 9);
	zassert_equal(bus.n_jan, 3);
	confere_cobranca(0, 0, "BRL 4.00");
	zassert_equal(bus.escritas, ESCRITAS_TEXTO(3) + 2 * ESCRITAS_TEXTO(2));
	confere_fim(t, false);
	confere_barramento_limpo();
}

ZTEST(st7789, test_18_formato_do_valor)
{
	static const struct {
		uint32_t cents;
		const char *texto;
	} casos[] = {
		{ 0, "BRL 0.00" },
		{ 5, "BRL 0.05" },
		{ 99, "BRL 0.99" },
		{ 100, "BRL 1.00" },
		{ 12345, "BRL 123.45" },
		{ 1000000, "BRL 10000.00" },
		/* O maior que cabe: 13 caracteres x 18 px = 234 px. */
		{ 99999999, "BRL 999999.99" },
	};

	for (size_t i = 0; i < ARRAY_SIZE(casos); i++) {
		uint32_t t;

		fakes_reset();
		t = pede_cobranca(casos[i].cents);
		esperar_desenho();
		zassert_equal(bus.n_jan, 3, "%u centavos", casos[i].cents);
		confere_cobranca(0, 0, casos[i].texto);
		confere_fim(t, false);
		confere_barramento_limpo();
	}
}

ZTEST(st7789, test_19_valor_que_nao_cabe_na_tela_falha_sem_desenhar_o_status)
{
	static const uint32_t grandes[] = { 100000000U, UINT32_MAX };

	/* "BRL 1000000.00" tem 14 caracteres x 18 px = 252 px > 240: a janela
	 * do valor e recusada antes de qualquer escrita e a cobranca para ali,
	 * sem o status: o pedido termina com falha.
	 */
	for (size_t i = 0; i < ARRAY_SIZE(grandes); i++) {
		uint32_t t;

		fakes_reset();
		t = pede_cobranca(grandes[i]);
		esperar_desenho();
		zassert_equal(bus.escritas, 0, "%u centavos", grandes[i]);
		zassert_equal(bus.n_cmds, 0);
		zassert_equal(bus.n_jan, 0);
		confere_fim(t, true);
		confere_barramento_limpo();
	}
}

ZTEST(st7789, test_20_ordem_placa_e_valor_no_barramento)
{
	uint32_t antes = emitidos;
	uint32_t p = pede_placa("BRA2E19");
	uint32_t c = pede_cobranca(1250);

	zassert_equal(c, p + 1U);
	zassert_equal(st7789_done(), antes, "nada termina antes de a thread rodar");
	esperar_desenho();

	/* Placa primeiro, depois valor e status; a conclusao e a da cobranca. */
	zassert_equal(bus.n_jan, 4);
	confere_texto(0, 0, "BRA2E19", "BRA2E19", 4, 40);
	confere_cobranca(1, 3, "BRL 12.50");
	confere_fim(c, false);
	confere_barramento_limpo();
}

/* ---- Erros de SPI durante o desenho ------------------------------------ */

ZTEST(st7789, test_21_erro_spi_ao_abrir_a_janela_da_placa)
{
	/* Escritas 1 a 5: CASET, dados, RASET, dados, RAMWR. A falha para o
	 * texto ali; os comandos antes dela foram enviados.
	 */
	static const int cmds_enviados[] = { 0, 1, 1, 2, 2 };
	uint32_t t;

	for (int n = 1; n <= 5; n++) {
		fakes_reset();
		spi_falha_na_escrita(n, -EIO);
		t = pede_placa("ABC1D23");
		esperar_desenho();
		zassert_equal(bus.escritas, n, "falha na escrita %d", n);
		zassert_equal(bus.n_cmds, cmds_enviados[n - 1], "falha na escrita %d", n);
		zassert_equal(bus.n_jan, 0, "falha na escrita %d", n);
		confere_fim(t, true);
		confere_barramento_limpo();
	}
	confere_cmd(0, CMD_CASET, 4);
	confere_cmd(1, CMD_RASET, 4);

	/* O erro nao estraga o display: o proximo pedido sai inteiro. */
	fakes_reset();
	t = pede_placa("ABC1D23");
	esperar_desenho();
	confere_texto(0, 0, "ABC1D23", "ABC1D23", 4, 40);
	confere_fim(t, false);
}

ZTEST(st7789, test_22_erro_spi_nos_pixels_da_placa)
{
	const uint32_t bytes_linha = 7 * FONT_ADVANCE * 4 * 2;
	uint32_t t;

	/* Primeira linha de pixels. */
	spi_falha_na_escrita(6, -EIO);
	t = pede_placa("ABC1D23");
	esperar_desenho();
	zassert_equal(bus.escritas, 6);
	zassert_equal(bus.n_jan, 1);
	zassert_equal(bus.jan[0].bytes, 0);
	confere_fim(t, true);

	/* Linha 15 de 28: as 14 anteriores chegaram, nenhuma depois. */
	fakes_reset();
	spi_falha_na_escrita(5 + 15, -EIO);
	t = pede_placa("ABC1D23");
	esperar_desenho();
	zassert_equal(bus.escritas, 5 + 15);
	zassert_equal(bus.jan[0].bytes, 14 * bytes_linha);
	confere_fim(t, true);
	confere_barramento_limpo();
}

ZTEST(st7789, test_23_erro_spi_no_valor_para_a_cobranca)
{
	/* Falha na primeira linha de pixels do valor: o status nao vem. */
	uint32_t t;

	spi_falha_na_escrita(6, -EIO);
	t = pede_cobranca(400);
	esperar_desenho();
	zassert_equal(bus.escritas, 6);
	zassert_equal(bus.n_cmds, 3);
	zassert_equal(bus.n_jan, 1);
	zassert_equal(bus.jan[0].bytes, 0);
	confere_fim(t, true);
	confere_barramento_limpo();
}

ZTEST(st7789, test_24_erro_spi_no_simulated_payment_para_a_cobranca)
{
	/* Falha no CASET do segundo texto: o valor saiu inteiro, o resto nao. */
	uint32_t t;

	spi_falha_na_escrita(ESCRITAS_TEXTO(3) + 1, -EIO);
	t = pede_cobranca(400);
	esperar_desenho();
	zassert_equal(bus.escritas, ESCRITAS_TEXTO(3) + 1);
	zassert_equal(bus.n_cmds, 3);
	zassert_equal(bus.n_jan, 1);
	confere_texto(0, 0, "BRL 4.00", "BRL 4.00", 3, 120);
	confere_fim(t, true);
	confere_barramento_limpo();
}

ZTEST(st7789, test_25_erro_spi_no_not_pix_e_a_cobranca_seguinte_sai_inteira)
{
	/* Falha na primeira linha de pixels do terceiro texto: valor e
	 * SIMULATED PAYMENT inteiros, NOT PIX sem pixel, pedido com falha.
	 */
	uint32_t t;

	spi_falha_na_escrita(ESCRITAS_TEXTO(3) + ESCRITAS_TEXTO(2) + 6, -EIO);
	t = pede_cobranca(400);
	esperar_desenho();
	zassert_equal(bus.escritas, ESCRITAS_TEXTO(3) + ESCRITAS_TEXTO(2) + 6);
	zassert_equal(bus.n_jan, 3);
	confere_texto(0, 0, "BRL 4.00", "BRL 4.00", 3, 120);
	confere_texto(1, 3, "SIMULATED PAYMENT", "SIMULATED PAYMENT", 2, 200);
	confere_cmd(8, CMD_RAMWR, 0);
	zassert_equal(bus.jan[2].bytes, 0);
	confere_fim(t, true);
	confere_barramento_limpo();

	/* O pedido seguinte desenha tudo e nao apaga a falha anterior. */
	fakes_reset();
	t = pede_cobranca(400);
	esperar_desenho();
	confere_cobranca(0, 0, "BRL 4.00");
	confere_fim(t, false);
	zassert_equal(st7789_failed(), t - 1U);
}

/* ---- Boot com os drivers prontos --------------------------------------- */

static void acha_display(const struct k_thread *thread, void *dado)
{
	const char *nome = k_thread_name_get((k_tid_t)thread);

	if (nome != NULL && strcmp(nome, "display") == 0) {
		*(k_tid_t *)dado = (k_tid_t)thread;
	}
}

ZTEST(st7789, test_26_boot_com_drivers_prontos_inicializa_antes_do_primeiro_pedido)
{
	k_tid_t display = NULL;
	uint32_t feito = st7789_done();
	uint32_t t;

	/* Encerra a thread do display (parada na fila) e cria outra, como num
	 * boot novo, agora com SPI e pinos prontos.
	 */
	k_thread_foreach(acha_display, &display);
	zassert_not_null(display, "thread do display nao encontrada");
	k_thread_abort(display);

	zassert_equal(st7789_start(), 0);
	esperar_init();

	/* Inicializou inteira sem pedido nenhum, e isso nao conclui pedido. */
	confere_init();
	zassert_equal(bus.n_cmds, 5);
	zassert_equal(bus.n_jan, 0);
	zassert_equal(bus.escritas, ESCRITAS_INIT);
	zassert_equal(st7789_done(), feito);
	confere_barramento_limpo();

	/* O primeiro pedido ja desenha, sem os 400 ms da inicializacao. */
	fakes_reset();
	t = pede_placa("ABC1D23");
	esperar_desenho();
	zassert_equal(fake_spi_is_ready_dt_fake.call_count, 0);
	zassert_equal(fake_gpio_pin_configure_dt_fake.call_count, 0);
	zassert_equal(bus.n_reset, 0);
	confere_texto(0, 0, "ABC1D23", "ABC1D23", 4, 40);
	zassert_equal(bus.escritas, ESCRITAS_TEXTO(4));
	confere_fim(t, false);
	confere_barramento_limpo();
}
