/* Montador de linhas: quadros completos, malformados, longos e atrasados. */
#include <string.h>

#include <zephyr/ztest.h>

#include "line_parser.h"

/** Entrega uma string inteira; devolve o resultado do último byte. */
static int feed(struct line_parser *lp, const char *s, int64_t now)
{
	int r = -1;

	for (size_t i = 0; s[i] != '\0'; i++) {
		r = line_parser_feed(lp, (uint8_t)s[i], now);
	}
	return r;
}

ZTEST_SUITE(line_parser, NULL, NULL, NULL, NULL, NULL);

ZTEST(line_parser, test_camera_crlf)
{
	struct line_parser lp;

	line_parser_init(&lp, LINE_CRLF);
	zassert_equal(feed(&lp, "OK\r\n", 0), 2);
	zassert_str_equal(lp.buf, "OK");
}

ZTEST(line_parser, test_fragmentos)
{
	struct line_parser lp;

	line_parser_init(&lp, LINE_CRLF);
	/* Pedaços de 8 bytes a cada 10 ms, como a câmera manda. */
	zassert_equal(feed(&lp, "+PLATE:1", 0), -1);
	zassert_equal(feed(&lp, ",\"ABC1D2", 10), -1);
	zassert_equal(feed(&lp, "3\",93\r\n", 20), 21);
	zassert_str_equal(lp.buf, "+PLATE:1,\"ABC1D23\",93");
}

ZTEST(line_parser, test_camera_sem_cr_e_cr_no_meio)
{
	struct line_parser lp;

	line_parser_init(&lp, LINE_CRLF);
	zassert_equal(feed(&lp, "OK\n", 0), -1);
	zassert_equal(feed(&lp, "O\rK\r\n", 0), -1);
	zassert_equal(feed(&lp, "\n", 0), -1, "linha vazia");
	zassert_equal(feed(&lp, "\r\n", 0), -1, "só CR LF");
	zassert_equal(feed(&lp, "OK\r\n", 0), 2, "volta a sincronizar");
}

ZTEST(line_parser, test_nao_imprimivel)
{
	struct line_parser lp;

	line_parser_init(&lp, LINE_LF);
	zassert_equal(feed(&lp, "P1 \x01X\n", 0), -1);
	zassert_equal(feed(&lp, "P1 \x7fX\n", 0), -1);
}

ZTEST(line_parser, test_terminal_lf_e_cr_proibido)
{
	struct line_parser lp;

	line_parser_init(&lp, LINE_LF);
	zassert_equal(feed(&lp, "P1 ERROR FRAME\n", 0), 14);
	zassert_equal(feed(&lp, "P1 ERROR FRAME\r\n", 0), -1);
}

ZTEST(line_parser, test_quadro_longo_descartado_ate_lf)
{
	struct line_parser lp;
	char longa[200];

	line_parser_init(&lp, LINE_LF);
	/* 127 bytes + LF = 128: ainda cabe. */
	memset(longa, 'A', 127);
	longa[127] = '\0';
	zassert_equal(feed(&lp, longa, 0), -1);
	zassert_equal(line_parser_feed(&lp, '\n', 0), 127);
	/* 128 bytes + LF = 129: descartado, mesmo terminando "bem". */
	memset(longa, 'A', 128);
	longa[128] = '\0';
	feed(&lp, longa, 0);
	zassert_equal(line_parser_feed(&lp, '\n', 0), -1);
	zassert_equal(feed(&lp, "OK\n", 0), 2, "a linha seguinte vale");
	/* Já descartando: o prazo de montagem não muda nada. */
	memset(longa, 'A', 150);
	longa[150] = '\0';
	feed(&lp, longa, 0);
	line_parser_poll(&lp, 5000);
	zassert_true(lp.discarding);
	zassert_equal(feed(&lp, "\nOK\n", 5000), 2);
}

ZTEST(line_parser, test_prazo_de_montagem)
{
	struct line_parser lp;

	line_parser_init(&lp, LINE_CRLF);
	feed(&lp, "+PLATE:1,", 100);
	line_parser_poll(&lp, 1099);
	zassert_false(lp.discarding, "ainda dentro de 1000 ms");
	line_parser_poll(&lp, 1100);
	zassert_true(lp.discarding);
	/* O resto da linha atrasada é descartado, inclusive o terminador. */
	zassert_equal(feed(&lp, "\"ABC1D23\",93\r\n", 1110), -1);
	zassert_equal(feed(&lp, "OK\r\n", 1120), 2);
	/* Sem linha começada, o prazo não faz nada. */
	line_parser_poll(&lp, 99999);
	zassert_false(lp.discarding);
}
