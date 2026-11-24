/*
 * Testes do montador de linhas (line_parser.c, REQ-11).
 */
#include <string.h>

#include <zephyr/ztest.h>

#include "config.h"
#include "line_parser.h"

static struct line_parser p;

/* Alimenta uma string; devolve o resultado do último byte. */
static enum lp_result feed(const char *s, int64_t t)
{
	enum lp_result r = LP_NONE;

	while (*s != '\0') {
		r = lp_feed(&p, (uint8_t)*s++, t);
	}
	return r;
}

/* Alimenta e conta quantas linhas válidas saíram. */
static int count_lines(const char *s, int64_t t)
{
	int n = 0;

	while (*s != '\0') {
		if (lp_feed(&p, (uint8_t)*s++, t) == LP_LINE) {
			n++;
		}
	}
	return n;
}

ZTEST_SUITE(parser, NULL, NULL, NULL, NULL, NULL);

ZTEST(parser, test_crlf_line)
{
	lp_init(&p, true);
	zassert_equal(feed("OK\r\n", 0), LP_LINE);
	zassert_str_equal(lp_line(&p), "OK");
}

ZTEST(parser, test_fragments_of_8_bytes_10ms_apart)
{
	const char *msg = "+PLATE:17,\"ABC1D23\",93\r\n";
	size_t n = strlen(msg);
	enum lp_result r = LP_NONE;

	lp_init(&p, true);
	for (size_t i = 0; i < n; i++) {
		r = lp_feed(&p, (uint8_t)msg[i], (int64_t)(i / 8) * 10);
		if (i + 1 < n) {
			zassert_equal(r, LP_NONE);
		}
	}
	zassert_equal(r, LP_LINE);
	zassert_str_equal(lp_line(&p), "+PLATE:17,\"ABC1D23\",93");
}

ZTEST(parser, test_crlf_mode_requires_cr)
{
	lp_init(&p, true);
	zassert_equal(feed("OK\n", 0), LP_DROPPED);
	zassert_equal(feed("OK\r\n", 0), LP_LINE, "recupera na linha seguinte");
}

ZTEST(parser, test_lf_mode_rejects_cr)
{
	lp_init(&p, false);
	zassert_equal(feed("P1 X\r\n", 0), LP_DROPPED);
	zassert_equal(feed("P1 X\n", 0), LP_LINE);
	zassert_str_equal(lp_line(&p), "P1 X");
}

ZTEST(parser, test_cr_in_the_middle_is_rejected)
{
	lp_init(&p, true);
	zassert_equal(feed("O\rK\r\n", 0), LP_DROPPED);
}

ZTEST(parser, test_empty_lines_are_rejected)
{
	lp_init(&p, true);
	zassert_equal(feed("\r\n", 0), LP_DROPPED);
	zassert_equal(feed("\n", 0), LP_DROPPED, "LF sozinho no modo CRLF");
	lp_init(&p, false);
	zassert_equal(feed("\n", 0), LP_DROPPED);
}

ZTEST(parser, test_non_printable_is_rejected)
{
	lp_init(&p, false);
	zassert_equal(feed("P1\tX\n", 0), LP_DROPPED);
	zassert_equal(lp_feed(&p, 0x80, 0), LP_NONE);
	zassert_equal(lp_feed(&p, '\n', 0), LP_DROPPED);
}

ZTEST(parser, test_frame_of_exactly_128_bytes_is_accepted)
{
	char msg[UART_FRAME_MAX + 1];

	/* 127 bytes de corpo + LF = 128. */
	memset(msg, 'A', UART_FRAME_MAX - 1);
	msg[UART_FRAME_MAX - 1] = '\n';
	msg[UART_FRAME_MAX] = '\0';
	lp_init(&p, false);
	zassert_equal(feed(msg, 0), LP_LINE);
	zassert_equal(strlen(lp_line(&p)), UART_FRAME_MAX - 1);
}

ZTEST(parser, test_frame_of_129_bytes_is_discarded_through_lf)
{
	char msg[UART_FRAME_MAX + 2];

	memset(msg, 'A', UART_FRAME_MAX);
	msg[UART_FRAME_MAX] = '\n';
	msg[UART_FRAME_MAX + 1] = '\0';
	lp_init(&p, false);
	zassert_equal(count_lines(msg, 0), 0);
	/* A linha seguinte volta a valer. */
	zassert_equal(feed("P1 Y\n", 0), LP_LINE);
}

ZTEST(parser, test_valid_prefix_of_overlong_frame_is_never_delivered)
{
	char msg[300];

	/* Uma linha válida seguida de lixo sem LF além do limite... */
	strcpy(msg, "OK");
	memset(msg + 2, 'x', 200);
	/* ...e uma linha válida que parece comando no meio do descarte. */
	strcpy(msg + 202, "\rOK\r\n");
	lp_init(&p, true);
	zassert_equal(count_lines(msg, 0), 0);
	zassert_equal(feed("OK\r\n", 0), LP_LINE);
}

ZTEST(parser, test_assembly_deadline_discards_through_lf)
{
	lp_init(&p, true);
	zassert_equal(feed("+PLATE:1", 0), LP_NONE);
	zassert_equal(feed("7,\"ABC1D23\",93\r", UART_ASSEMBLY_MS - 1), LP_NONE);
	/* O LF chega tarde: o quadro inteiro é descartado. */
	zassert_equal(lp_feed(&p, '\n', UART_ASSEMBLY_MS), LP_DROPPED);
	zassert_equal(feed("OK\r\n", UART_ASSEMBLY_MS + 10), LP_LINE);
}

ZTEST(parser, test_assembly_deadline_skips_rest_of_line)
{
	lp_init(&p, true);
	zassert_equal(feed("+PLA", 0), LP_NONE);
	/* O resto chega depois do prazo: tudo até o LF é jogado fora. */
	zassert_equal(count_lines("TE:17,\"ABC1D23\",93\r\n", 2000), 0);
	zassert_equal(feed("OK\r\n", 2010), LP_LINE);
}

ZTEST(parser, test_parsers_are_independent)
{
	struct line_parser other;

	lp_init(&p, true);
	lp_init(&other, false);
	zassert_equal(feed("O", 0), LP_NONE);
	zassert_equal(lp_feed(&other, 'X', 0), LP_NONE);
	zassert_equal(lp_feed(&other, '\n', 0), LP_LINE);
	zassert_equal(feed("K\r\n", 0), LP_LINE);
	zassert_str_equal(lp_line(&p), "OK");
}

ZTEST(parser, test_parse_uint_canonical)
{
	const char *s;
	uint32_t v;

	s = "17,";
	zassert_true(lp_parse_uint(&s, 100, &v));
	zassert_equal(v, 17);
	zassert_equal(*s, ',');

	s = "0";
	zassert_true(lp_parse_uint(&s, 100, &v));
	zassert_equal(v, 0);

	s = "017";
	zassert_false(lp_parse_uint(&s, 100, &v), "zero à esquerda");
	s = "00";
	zassert_false(lp_parse_uint(&s, 100, &v));
	s = "101";
	zassert_false(lp_parse_uint(&s, 100, &v), "acima do máximo");
	s = "";
	zassert_false(lp_parse_uint(&s, 100, &v));
	s = "-1";
	zassert_false(lp_parse_uint(&s, 100, &v));
	s = "99999999999999999999";
	zassert_false(lp_parse_uint(&s, 999999999, &v), "sem estouro");
}
