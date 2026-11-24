/* Tests of services/line_framer: bounded CRLF/LF lines with a deadline. */
#include <string.h>

#include <zephyr/ztest.h>

#include "services/line_framer.h"

static struct line_framer fr;

static void setup(void *fixture)
{
	ARG_UNUSED(fixture);
	line_framer_init(&fr, LINE_CRLF);
}

ZTEST_SUITE(line_framer, NULL, NULL, setup, NULL, NULL);

/* Feed a whole string at time @p now; return the last non-NONE result. */
static enum line_result feed(const char *s, int64_t now)
{
	enum line_result last = LINE_NONE;

	for (size_t i = 0; s[i] != '\0'; i++) {
		enum line_result r = line_framer_feed(&fr, (uint8_t)s[i], now);

		if (r != LINE_NONE) {
			last = r;
		}
	}
	return last;
}

static void feed_n(size_t n, char c, int64_t now)
{
	for (size_t i = 0; i < n; i++) {
		zassert_equal(line_framer_feed(&fr, (uint8_t)c, now), LINE_NONE);
	}
}

ZTEST(line_framer, test_crlf_line_is_delivered_without_terminator)
{
	zassert_equal(feed("OK\r\n", 0), LINE_READY);
	zassert_str_equal(line_framer_line(&fr), "OK");
}

ZTEST(line_framer, test_fragments_assemble_one_line)
{
	zassert_equal(feed("+PLA", 0), LINE_NONE);
	zassert_equal(feed("TE:1", 10), LINE_NONE);
	zassert_equal(feed(",x\r\n", 20), LINE_READY);
	zassert_str_equal(line_framer_line(&fr), "+PLATE:1,x");
}

ZTEST(line_framer, test_two_lines_in_a_row)
{
	zassert_equal(feed("A\r\n", 0), LINE_READY);
	zassert_str_equal(line_framer_line(&fr), "A");
	zassert_equal(feed("B\r\n", 0), LINE_READY);
	zassert_str_equal(line_framer_line(&fr), "B");
}

ZTEST(line_framer, test_bare_lf_is_refused_in_crlf_mode)
{
	zassert_equal(feed("OK\n", 0), LINE_DROPPED);
	zassert_equal(feed("OK\r\n", 0), LINE_READY);
}

ZTEST(line_framer, test_cr_inside_the_line_discards_it)
{
	zassert_equal(feed("O\rK\r\n", 0), LINE_DROPPED);
	zassert_equal(feed("OK\r\n", 0), LINE_READY);
}

ZTEST(line_framer, test_empty_line_is_dropped)
{
	zassert_equal(feed("\r\n", 0), LINE_DROPPED);
}

ZTEST(line_framer, test_lf_mode_accepts_lf_and_refuses_cr)
{
	line_framer_init(&fr, LINE_LF);
	zassert_equal(feed("P1 X\n", 0), LINE_READY);
	zassert_str_equal(line_framer_line(&fr), "P1 X");
	zassert_equal(feed("P1 X\r\n", 0), LINE_DROPPED);
	zassert_equal(feed("P1 Y\n", 0), LINE_READY);
}

ZTEST(line_framer, test_crlf_limit_is_126_body_bytes)
{
	feed_n(126, 'A', 0);
	zassert_equal(feed("\r\n", 0), LINE_READY);
	zassert_equal(strlen(line_framer_line(&fr)), 126);

	feed_n(127, 'A', 0);
	zassert_equal(feed("\r\n", 0), LINE_DROPPED);
}

ZTEST(line_framer, test_lf_limit_is_127_body_bytes)
{
	line_framer_init(&fr, LINE_LF);
	feed_n(127, 'A', 0);
	zassert_equal(feed("\n", 0), LINE_READY);

	feed_n(128, 'A', 0);
	zassert_equal(feed("\n", 0), LINE_DROPPED);
}

ZTEST(line_framer, test_overlong_line_does_not_leak_a_prefix_and_next_line_is_clean)
{
	feed_n(300, 'A', 0);
	zassert_equal(feed("\r\n", 0), LINE_DROPPED);
	zassert_equal(feed("OK\r\n", 0), LINE_READY);
	zassert_str_equal(line_framer_line(&fr), "OK");
}

ZTEST(line_framer, test_non_printable_byte_discards_the_line)
{
	zassert_equal(line_framer_feed(&fr, 'A', 0), LINE_NONE);
	zassert_equal(line_framer_feed(&fr, 0x00, 0), LINE_NONE);
	zassert_equal(feed("B\r\n", 0), LINE_DROPPED);
	line_framer_init(&fr, LINE_CRLF);
	zassert_equal(line_framer_feed(&fr, 0x80, 0), LINE_NONE);
	zassert_equal(feed("\r\n", 0), LINE_DROPPED);
}

ZTEST(line_framer, test_line_completed_at_exactly_1000_ms_is_accepted)
{
	zassert_equal(feed("OK", 0), LINE_NONE);
	zassert_equal(feed("\r\n", 1000), LINE_READY);
}

ZTEST(line_framer, test_line_older_than_1000_ms_is_discarded_through_lf)
{
	zassert_equal(feed("OK", 0), LINE_NONE);
	zassert_equal(feed("\r\n", 1001), LINE_DROPPED);
	zassert_equal(feed("OK\r\n", 1002), LINE_READY);
}

ZTEST(line_framer, test_deadline_applies_without_new_bytes)
{
	zassert_equal(feed("OK", 0), LINE_NONE);
	line_framer_check(&fr, 1001);
	/* The rest of the expired line is skipped, a valid prefix never completes. */
	zassert_equal(feed("X\r\n", 1002), LINE_DROPPED);
}

ZTEST(line_framer, test_check_ignores_an_idle_framer)
{
	line_framer_check(&fr, 100000);
	zassert_equal(feed("OK\r\n", 100001), LINE_READY);
}
