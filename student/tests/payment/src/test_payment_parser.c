/*
 * test_payment_parser.c — analisador P1 (REQ-11, REQ-05) e UUID.
 * Lógica pura, sem falsos. Garante principalmente que NADA malformado vira RESULT.
 */
#include <string.h>

#include <zephyr/ztest.h>

#include "payment_parser.h"
#include "payment_uuid.h"

#define UUID "3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c1"

static struct payment_parser parser;
static struct payment_frame frame;

static void reset_parser(void *fixture)
{
	ARG_UNUSED(fixture);
	payment_parser_init(&parser);
	memset(&frame, 0, sizeof(frame));
}

static enum payment_frame_kind feed_text(const char *text, int64_t now)
{
	enum payment_frame_kind last = PAYMENT_FRAME_NONE;

	for (const char *p = text; *p != '\0'; ++p) {
		enum payment_frame_kind kind = payment_parser_feed(&parser, (uint8_t)*p, now, &frame);

		if (kind != PAYMENT_FRAME_NONE) {
			last = kind;
		}
	}
	return last;
}

ZTEST_SUITE(payment_parser, NULL, NULL, reset_parser, NULL, NULL);

ZTEST(payment_parser, test_paid_result_fields)
{
	zassert_equal(feed_text("P1 RESULT " UUID " PAID 400 BRL\n", 0), PAYMENT_FRAME_RESULT);
	zassert_str_equal(frame.uuid, UUID);
	zassert_equal(frame.status, PAYMENT_STATUS_PAID);
	zassert_equal(frame.amount, 400U);
}

ZTEST(payment_parser, test_cancelled_result_and_amount_limits)
{
	zassert_equal(feed_text("P1 RESULT " UUID " CANCELLED 1 BRL\n", 0), PAYMENT_FRAME_RESULT);
	zassert_equal(frame.status, PAYMENT_STATUS_CANCELLED);
	zassert_equal(frame.amount, 1U);
	zassert_equal(feed_text("P1 RESULT " UUID " PAID 1000000 BRL\n", 0), PAYMENT_FRAME_RESULT);
	zassert_equal(frame.amount, 1000000U);
}

ZTEST(payment_parser, test_error_frames_for_every_known_code)
{
	static const char *const lines[] = {
		"P1 ERROR FRAME\n", "P1 ERROR TIMEOUT\n", "P1 ERROR CAPACITY\n",
		"P1 ERROR CONFLICT\n", "P1 ERROR UNKNOWN\n",
	};

	for (size_t i = 0U; i < ARRAY_SIZE(lines); ++i) {
		zassert_equal(feed_text(lines[i], 0), PAYMENT_FRAME_ERROR);
	}
	zassert_equal(feed_text("P1 ERROR BOGUS\n", 0), PAYMENT_FRAME_INVALID);
}

ZTEST(payment_parser, test_invalid_lines_never_become_results)
{
	static const char *const bad[] = {
		"\n",
		"HELLO\n",
		"P2 RESULT " UUID " PAID 400 BRL\n",              /* versão errada */
		"P1RESULT " UUID " PAID 400 BRL\n",               /* sem espaço */
		"P1 STATUS " UUID " PAID 400 BRL\n",              /* verbo desconhecido */
		"P1 RESULT " UUID " PAID broken BRL\n",           /* valor quebrado (modelo MALFORMED) */
		"P1 RESULT " UUID " PAID 0 BRL\n",                /* valor zero */
		"P1 RESULT " UUID " PAID 0400 BRL\n",             /* zero à esquerda */
		"P1 RESULT " UUID " PAID 1000001 BRL\n",          /* acima do limite */
		"P1 RESULT " UUID " PAID 12345678901 BRL\n",      /* 11 dígitos */
		"P1 RESULT " UUID " PAID -400 BRL\n",
		"P1 RESULT " UUID " PAID 400 USD\n",              /* moeda errada */
		"P1 RESULT " UUID " PAID 400 BRL \n",             /* espaço no fim */
		"P1 RESULT " UUID " PAID 400 BRL extra\n",
		"P1 RESULT " UUID " PAID 400\n",                  /* sem moeda */
		"P1 RESULT " UUID " paid 400 BRL\n",              /* minúscula */
		"P1 RESULT " UUID " REFUNDED 400 BRL\n",          /* status desconhecido */
		"P1 RESULT " UUID "  PAID 400 BRL\n",             /* dois espaços */
		"P1 RESULT " UUID "PAID 400 BRL\n",               /* sem espaço depois do UUID */
		"P1 RESULT 3F2A9C10-7B4E-4D21-9A6F-0C5E8B7D12C1 PAID 400 BRL\n", /* UUID maiúsculo */
		"P1 RESULT 3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12c PAID 400 BRL\n",  /* UUID curto */
		"P1 RESULT 3f2a9c107b4e-4d21-9a6f-0c5e8b7d12c1x PAID 400 BRL\n", /* hífen faltando */
		"P1 RESULT 3f2a9c10-7b4e-4d21-9a6f-0c5e8b7d12g1 PAID 400 BRL\n", /* não é hexa */
		"P1 RESULT " UUID "\n",                           /* corta depois do UUID */
		"P1 ERROR\n",
		"P1 ERROR FRAME extra\n",
	};

	for (size_t i = 0U; i < ARRAY_SIZE(bad); ++i) {
		zassert_equal(feed_text(bad[i], (int64_t)i), PAYMENT_FRAME_INVALID, "linha %u",
			      (unsigned int)i);
	}
	zassert_equal(feed_text("P1 ERROR FRAME\n", 100), PAYMENT_FRAME_ERROR); /* ainda sincronizado */
}

ZTEST(payment_parser, test_cr_is_forbidden_because_p1_ends_in_lf_only)
{
	zassert_equal(feed_text("P1 ERROR FRAME\r\n", 0), PAYMENT_FRAME_INVALID);
	zassert_equal(feed_text("P1 ERROR FRAME\n", 1), PAYMENT_FRAME_ERROR);
}

ZTEST(payment_parser, test_forbidden_byte_discards_through_lf_then_resyncs)
{
	zassert_equal(payment_parser_feed(&parser, 'P', 0, &frame), PAYMENT_FRAME_NONE);
	zassert_equal(payment_parser_feed(&parser, 0x01, 0, &frame), PAYMENT_FRAME_NONE);
	zassert_equal(payment_parser_feed(&parser, 0x80, 0, &frame), PAYMENT_FRAME_NONE);
	/* uma linha que "parece" boa, mas está dentro do trecho descartado */
	zassert_equal(feed_text("P1 ERROR FRAME\n", 0), PAYMENT_FRAME_INVALID);
	zassert_equal(feed_text("P1 ERROR FRAME\n", 1), PAYMENT_FRAME_ERROR);

	/* byte acima de 126 (DEL = 127) numa linha limpa também invalida a linha toda */
	zassert_equal(payment_parser_feed(&parser, 'P', 2, &frame), PAYMENT_FRAME_NONE);
	zassert_equal(payment_parser_feed(&parser, 0x7F, 2, &frame), PAYMENT_FRAME_NONE);
	zassert_true(parser.discarding);
	zassert_equal(feed_text("1 ERROR FRAME\n", 2), PAYMENT_FRAME_INVALID);
	zassert_equal(feed_text("P1 ERROR FRAME\n", 3), PAYMENT_FRAME_ERROR);
}

ZTEST(payment_parser, test_frame_limit_is_128_bytes_with_the_lf)
{
	for (int i = 0; i < 127; ++i) {
		zassert_equal(payment_parser_feed(&parser, 'A', 0, &frame), PAYMENT_FRAME_NONE);
	}
	zassert_false(parser.discarding);
	zassert_equal(parser.len, 127U);
	zassert_equal(payment_parser_feed(&parser, '\n', 0, &frame), PAYMENT_FRAME_INVALID); /* cabe */

	for (int i = 0; i < 127; ++i) {
		zassert_equal(payment_parser_feed(&parser, 'A', 1, &frame), PAYMENT_FRAME_NONE);
	}
	zassert_equal(payment_parser_feed(&parser, 'A', 1, &frame), PAYMENT_FRAME_NONE);
	zassert_true(parser.discarding); /* o 128º byte não foi LF: estourou */
	zassert_equal(feed_text("\n", 1), PAYMENT_FRAME_INVALID);
	zassert_equal(feed_text("P1 ERROR FRAME\n", 2), PAYMENT_FRAME_ERROR);
}

ZTEST(payment_parser, test_partial_line_deadline_counts_from_first_byte)
{
	zassert_equal(feed_text("P1 RESULT ", 0), PAYMENT_FRAME_NONE);
	zassert_equal(feed_text(UUID " PAID ", 600), PAYMENT_FRAME_NONE);
	zassert_equal(feed_text("400 BRL\n", 999), PAYMENT_FRAME_RESULT); /* a tempo */

	zassert_equal(feed_text("P1 RESULT ", 2000), PAYMENT_FRAME_NONE);
	zassert_equal(feed_text(UUID " PAID 400 BRL\n", 3000), PAYMENT_FRAME_INVALID); /* venceu */
	zassert_equal(feed_text("P1 ERROR FRAME\n", 3010), PAYMENT_FRAME_ERROR);
}

ZTEST(payment_parser, test_expire_on_idle_parser_and_explicit_expire)
{
	payment_parser_expire(&parser, 100000);
	zassert_equal(feed_text("P1 ERROR FRAME\n", 100000), PAYMENT_FRAME_ERROR);
	zassert_equal(feed_text("P1", 200000), PAYMENT_FRAME_NONE);
	payment_parser_expire(&parser, 200999);
	zassert_equal(parser.len, 2U);
	payment_parser_expire(&parser, 201000);
	zassert_true(parser.discarding);
	zassert_equal(feed_text("\n", 201001), PAYMENT_FRAME_INVALID);
}

ZTEST(payment_parser, test_valid_prefix_without_lf_is_never_executed)
{
	zassert_equal(feed_text("P1 RESULT " UUID " PAID 400 BRL", 0), PAYMENT_FRAME_NONE);
}

/* ---- UUID ---- */

ZTEST(payment_parser, test_plate_validation)
{
	zassert_true(payment_plate_valid("ABC1D23"));
	zassert_true(payment_plate_valid("ABC1234"));
	zassert_false(payment_plate_valid("abc1d23"));
	zassert_false(payment_plate_valid("1BC1D23"));
	zassert_false(payment_plate_valid("ABCDD23"));
	zassert_false(payment_plate_valid("ABC1#23"));
	zassert_false(payment_plate_valid("ABC1DA3"));
	zassert_false(payment_plate_valid("ABC1D2A"));
	zassert_false(payment_plate_valid("ABC1D2"));
	zassert_false(payment_plate_valid("ABC1D234")); /* 8 caracteres: 8ª posição não é NUL */
}

ZTEST(payment_parser, test_generated_uuids_are_valid_and_unique)
{
	char first[PAYMENT_UUID_SIZE];
	char second[PAYMENT_UUID_SIZE];

	payment_uuid_make(0U, first);
	payment_uuid_make(1U, second);
	zassert_true(payment_uuid_valid(first));
	zassert_true(payment_uuid_valid(second));
	zassert_true(strcmp(first, second) != 0);
	zassert_equal(first[14], '4'); /* forma de UUID v4 */
	zassert_true(strchr("89ab", first[19]) != NULL);
	/* os 12 últimos dígitos carregam o contador (unicidade por construção) */
	payment_uuid_make(0xABCDEF012345ULL, second);
	zassert_str_equal(&second[24], "abcdef012345");
}

ZTEST(payment_parser, test_uuid_validation_rejects_bad_shapes)
{
	zassert_true(payment_uuid_valid(UUID));
	zassert_false(payment_uuid_valid(UUID "0"));       /* longo demais */
	zassert_false(payment_uuid_valid("3f2a9c10"));     /* curto */
	zassert_false(payment_uuid_valid("3F2A9C10-7B4E-4D21-9A6F-0C5E8B7D12C1"));
	zassert_true(payment_uuid_prefix_valid(UUID " resto"));
}
