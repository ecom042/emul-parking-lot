/**
 * @file test_line.c
 * @brief Testes do montador de quadros UART (line.c).
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "line.h"

static struct line_framer framer;

/**
 * @brief Entrega uma string inteira no mesmo instante.
 *
 * @param s Bytes a entregar.
 * @param now_ms Instante de todos os bytes.
 * @return Resultado do último byte.
 */
static enum line_result feed_str(const char *s, int64_t now_ms)
{
	enum line_result r = LINE_NONE;

	for (size_t i = 0U; s[i] != '\0'; i++) {
		r = line_feed(&framer, (uint8_t)s[i], now_ms);
	}

	return r;
}

ZTEST_SUITE(line, NULL, NULL, NULL, NULL, NULL);

ZTEST(line, test_camera_frame_with_crlf_is_ready)
{
	line_init(&framer, true);

	zassert_equal(feed_str("OK\r\n", 0), LINE_READY);
	zassert_str_equal(line_frame(&framer), "OK");
}

ZTEST(line, test_payment_frame_with_lf_is_ready)
{
	line_init(&framer, false);

	zassert_equal(feed_str("P1 ERROR FRAME\n", 0), LINE_READY);
	zassert_str_equal(line_frame(&framer), "P1 ERROR FRAME");
}

ZTEST(line, test_camera_lf_without_cr_is_dropped)
{
	line_init(&framer, true);

	zassert_equal(feed_str("OK\n", 0), LINE_DROPPED);
}

ZTEST(line, test_payment_cr_discards_until_lf_and_resyncs)
{
	line_init(&framer, false);

	zassert_equal(feed_str("P1 RESULT\r x\n", 0), LINE_DROPPED);
	zassert_equal(feed_str("P1 ERROR FRAME\n", 0), LINE_READY);
	zassert_str_equal(line_frame(&framer), "P1 ERROR FRAME");
}

ZTEST(line, test_byte_after_cr_is_malformed)
{
	line_init(&framer, true);

	zassert_equal(feed_str("O\rK\r\n", 0), LINE_DROPPED);
}

ZTEST(line, test_non_printable_byte_is_dropped)
{
	line_init(&framer, false);

	zassert_equal(feed_str("P1\x01 X\n", 0), LINE_DROPPED);
}

ZTEST(line, test_empty_frame_is_dropped)
{
	line_init(&framer, true);
	zassert_equal(feed_str("\r\n", 0), LINE_DROPPED);

	line_init(&framer, false);
	zassert_equal(feed_str("\n", 0), LINE_DROPPED);
}

ZTEST(line, test_longest_frame_fits_and_one_more_is_dropped)
{
	char body[LINE_MAX_FRAME + 2];

	/* LF: 127 bytes de corpo mais o LF somam 128. */
	line_init(&framer, false);
	memset(body, 'A', LINE_MAX_FRAME - 1U);
	body[LINE_MAX_FRAME - 1U] = '\0';
	zassert_equal(feed_str(body, 0), LINE_NONE);
	zassert_equal(line_feed(&framer, '\n', 0), LINE_READY);
	zassert_equal(strlen(line_frame(&framer)), LINE_MAX_FRAME - 1U);

	/* Um byte a mais não cabe e o prefixo nunca é entregue. */
	line_init(&framer, false);
	memset(body, 'A', LINE_MAX_FRAME);
	body[LINE_MAX_FRAME] = '\0';
	zassert_equal(feed_str(body, 0), LINE_NONE);
	zassert_equal(line_feed(&framer, '\n', 0), LINE_DROPPED);
}

ZTEST(line, test_crlf_longest_frame_has_126_byte_body)
{
	char body[LINE_MAX_FRAME];

	line_init(&framer, true);
	memset(body, 'B', LINE_MAX_FRAME - 2U);
	body[LINE_MAX_FRAME - 2U] = '\0';
	(void)feed_str(body, 0);
	zassert_equal(feed_str("\r\n", 0), LINE_READY);
	zassert_equal(strlen(line_frame(&framer)), LINE_MAX_FRAME - 2U);
}

ZTEST(line, test_partial_frame_expires_and_suffix_is_dropped)
{
	line_init(&framer, true);

	zassert_equal(feed_str("+PLATE:1,", 0), LINE_NONE);
	zassert_false(line_expire(&framer, LINE_ASSEMBLY_MS));
	zassert_true(line_expire(&framer, LINE_ASSEMBLY_MS + 1));
	zassert_false(line_expire(&framer, LINE_ASSEMBLY_MS + 2));

	/* O resto do quadro velho é descartado; o seguinte é aceito. */
	zassert_equal(feed_str("\"ABC1D23\",93\r\n", LINE_ASSEMBLY_MS + 5), LINE_DROPPED);
	zassert_equal(feed_str("OK\r\n", LINE_ASSEMBLY_MS + 6), LINE_READY);
}

ZTEST(line, test_late_byte_of_slow_frame_starts_discarding)
{
	line_init(&framer, true);

	zassert_equal(feed_str("O", 0), LINE_NONE);
	zassert_equal(feed_str("K\r\n", LINE_ASSEMBLY_MS + 1), LINE_DROPPED);
}

ZTEST(line, test_expire_without_partial_frame_does_nothing)
{
	line_init(&framer, true);

	zassert_false(line_expire(&framer, 5000));
	zassert_equal(feed_str("OK\r\n", 5000), LINE_READY);
	zassert_false(line_expire(&framer, 9000));
}

ZTEST(line, test_camera_bare_lf_is_dropped)
{
	line_init(&framer, true);

	zassert_equal(feed_str("\n", 0), LINE_DROPPED);
	zassert_equal(feed_str("OK\r\n", 0), LINE_READY);
}
