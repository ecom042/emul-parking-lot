/*
 * test_camera_parser.c — testes do analisador de linhas (REQ-11).
 *
 * O parser é lógica pura: não há falsos aqui. Cada teste alimenta bytes e
 * confere (zassert_*) o quadro que sai. Casos importantes:
 *   - quadros válidos (OK, ERROR, +PLATE) e os limites de confiança;
 *   - uma lista de linhas inválidas que NUNCA podem virar placa;
 *   - fronteira dos 128 bytes, caracteres proibidos e ressincronização;
 *   - prazo de 1000 ms para linha parcial, contado do primeiro byte.
 */
#include <string.h>

#include <zephyr/ztest.h>

#include "camera_parser.h"

static struct camera_parser parser;
static struct camera_frame frame;

static void reset_parser(void *fixture)
{
	ARG_UNUSED(fixture);
	camera_parser_init(&parser);
	memset(&frame, 0, sizeof(frame));
}

/* Alimenta o texto byte a byte, todo no instante `now`; devolve o último quadro concluído. */
static enum camera_frame_kind feed_text(const char *text, int64_t now)
{
	enum camera_frame_kind last = CAMERA_FRAME_NONE;

	for (const char *p = text; *p != '\0'; ++p) {
		enum camera_frame_kind kind = camera_parser_feed(&parser, (uint8_t)*p, now, &frame);

		if (kind != CAMERA_FRAME_NONE) {
			last = kind;
		}
	}
	return last;
}

static enum camera_frame_kind feed_byte(uint8_t byte, int64_t now)
{
	return camera_parser_feed(&parser, byte, now, &frame);
}

ZTEST_SUITE(camera_parser, NULL, NULL, reset_parser, NULL, NULL);

ZTEST(camera_parser, test_ok_and_error_frames)
{
	zassert_equal(feed_text("OK\r\n", 0), CAMERA_FRAME_OK);
	zassert_equal(feed_text("ERROR\r\n", 1), CAMERA_FRAME_ERROR);
}

ZTEST(camera_parser, test_plate_frame_fields)
{
	zassert_equal(feed_text("+PLATE:17,\"ABC1D23\",93\r\n", 0), CAMERA_FRAME_PLATE);
	zassert_equal(frame.id, 17U);
	zassert_str_equal(frame.plate, "ABC1D23");
	zassert_equal(frame.confidence, 93U);
}

ZTEST(camera_parser, test_legacy_plate_and_id_limits)
{
	zassert_equal(feed_text("+PLATE:999999999,\"ABC1234\",0\r\n", 0), CAMERA_FRAME_PLATE);
	zassert_equal(frame.id, 999999999U);
	zassert_str_equal(frame.plate, "ABC1234");
	zassert_equal(frame.confidence, 0U);
	zassert_equal(feed_text("+PLATE:1,\"XYZ9Z99\",100\r\n", 0), CAMERA_FRAME_PLATE);
	zassert_equal(frame.id, 1U);
	zassert_equal(frame.confidence, 100U);
}

ZTEST(camera_parser, test_invalid_lines_are_dropped)
{
	static const char *const bad[] = {
		"\r\n",                                   /* vazia */
		"HELLO\r\n",                              /* comando desconhecido */
		"ok\r\n",                                 /* minúscula */
		"OK \r\n",                                /* espaço sobrando */
		" OK\r\n",
		"OKAY\r\n",
		"+PLATE\r\n",                             /* sem os dois pontos */
		"+PLATE:\r\n",
		"+PLATE:0,\"ABC1D23\",93\r\n",            /* id zero */
		"+PLATE:01,\"ABC1D23\",93\r\n",           /* zero à esquerda */
		"+PLATE:1000000000,\"ABC1D23\",93\r\n",   /* id grande demais */
		"+PLATE:12345678901,\"ABC1D23\",93\r\n",  /* 11 dígitos */
		"+PLATE:abc,\"ABC1D23\",93\r\n",          /* id não numérico */
		"+PLATE:17;\"ABC1D23\",93\r\n",           /* sem vírgula */
		"+PLATE:17, \"ABC1D23\",93\r\n",          /* espaço depois da vírgula */
		"+PLATE:17,ABC1D23,93\r\n",               /* sem aspas */
		"+PLATE:17,\"AB\r\n",                     /* placa curta (acaba no NUL) */
		"+PLATE:17,\"abc1d23\",93\r\n",           /* minúsculas */
		"+PLATE:17,\"1BC1D23\",93\r\n",           /* 1ª posição não é letra */
		"+PLATE:17,\"AB11D23\",93\r\n",           /* 2ª/3ª posições */
		"+PLATE:17,\"ABCDD23\",93\r\n",           /* 4ª posição não é dígito */
		"+PLATE:17,\"ABC1d23\",93\r\n",           /* 5ª posição minúscula */
		"+PLATE:17,\"ABC1D2A\",93\r\n",           /* 7ª posição não é dígito */
		"+PLATE:17,\"ABC1DA3\",93\r\n",           /* 6ª posição não é dígito */
		"+PLATE:17,\"ABC1D2\",93\r\n",            /* 6 caracteres */
		"+PLATE:17,\"ABC1D234\",93\r\n",          /* 8 caracteres */
		"+PLATE:17,\"ABC1D23\"93\r\n",            /* faltou vírgula depois da placa */
		"+PLATE:17,\"ABC1D23\",\r\n",             /* sem confiança */
		"+PLATE:17,\"ABC1D23\",093\r\n",          /* confiança com zero à esquerda */
		"+PLATE:17,\"ABC1D23\",101\r\n",          /* confiança > 100 */
		"+PLATE:17,\"ABC1D23\",-1\r\n",           /* confiança negativa */
		"+PLATE:17,\"ABC1D23\",9x\r\n",           /* lixo na confiança */
		"+PLATE:17,\"ABC1D23\",93 \r\n",          /* espaço no fim */
		"+PLATE:17,\"ABC1D23\",93,1\r\n",         /* campo extra */
		"+PLATE:17,\"ABC1D23\",93\r\r\n",         /* CR duplo: CR no corpo */
	};

	for (size_t i = 0U; i < ARRAY_SIZE(bad); ++i) {
		zassert_equal(feed_text(bad[i], (int64_t)i), CAMERA_FRAME_INVALID, "linha %u", (unsigned int)i);
	}
	/* depois de tudo isso o parser continua sincronizado */
	zassert_equal(feed_text("OK\r\n", 100), CAMERA_FRAME_OK);
}

ZTEST(camera_parser, test_bare_lf_without_cr_is_invalid)
{
	zassert_equal(feed_text("OK\n", 0), CAMERA_FRAME_INVALID);
	zassert_equal(feed_text("\n", 1), CAMERA_FRAME_INVALID);
}

ZTEST(camera_parser, test_forbidden_bytes_discard_through_lf_then_resync)
{
	/* byte de controle no meio: a linha toda é descartada, mesmo "parecendo" OK */
	zassert_equal(feed_byte('O', 0), CAMERA_FRAME_NONE);
	zassert_equal(feed_byte(0x01, 0), CAMERA_FRAME_NONE);
	zassert_equal(feed_byte('K', 0), CAMERA_FRAME_NONE);
	zassert_equal(feed_byte('\r', 0), CAMERA_FRAME_NONE);
	zassert_equal(feed_byte('\n', 0), CAMERA_FRAME_INVALID);
	zassert_equal(feed_text("OK\r\n", 1), CAMERA_FRAME_OK);

	/* byte > 126 (DEL e acima) */
	zassert_equal(feed_byte(0x7F, 2), CAMERA_FRAME_NONE);
	zassert_equal(feed_byte(0x80, 2), CAMERA_FRAME_NONE);
	zassert_equal(feed_text("OK\r\n", 2), CAMERA_FRAME_INVALID); /* ainda descartando até o LF */
	zassert_equal(feed_text("OK\r\n", 3), CAMERA_FRAME_OK);
}

ZTEST(camera_parser, test_frame_size_limit_is_128_bytes_with_terminator)
{
	/* 126 letras + CR + LF = 128 bytes: cabe, mas o corpo não é um comando válido */
	for (int i = 0; i < 126; ++i) {
		zassert_equal(feed_byte('A', 0), CAMERA_FRAME_NONE);
	}
	zassert_false(parser.discarding);
	zassert_equal(feed_byte('\r', 0), CAMERA_FRAME_NONE);
	zassert_equal(parser.len, 127U);
	zassert_false(parser.discarding);
	zassert_equal(feed_byte('\n', 0), CAMERA_FRAME_INVALID);

	/* 127 letras + mais uma: o 128º byte não é LF, então a linha estourou */
	for (int i = 0; i < 127; ++i) {
		zassert_equal(feed_byte('A', 1), CAMERA_FRAME_NONE);
	}
	zassert_false(parser.discarding);
	zassert_equal(feed_byte('A', 1), CAMERA_FRAME_NONE);
	zassert_true(parser.discarding);
	zassert_equal(parser.len, 0U);
	zassert_equal(feed_text("\r\n", 1), CAMERA_FRAME_INVALID);

	/* ressincronizou: o próximo quadro bom passa */
	zassert_equal(feed_text("OK\r\n", 2), CAMERA_FRAME_OK);
}

ZTEST(camera_parser, test_valid_prefix_is_never_executed)
{
	/* sem terminador não há quadro, por mais que o início pareça uma placa */
	zassert_equal(feed_text("+PLATE:17,\"ABC1D23\",93", 0), CAMERA_FRAME_NONE);
	/* lixo depois do prefixo invalida a linha inteira */
	zassert_equal(feed_text("xx\r\n", 1), CAMERA_FRAME_INVALID);
}

ZTEST(camera_parser, test_back_to_back_frames_in_one_burst)
{
	zassert_equal(feed_text("OK\r\n", 0), CAMERA_FRAME_OK);
	zassert_equal(feed_text("+PLATE:2,\"ABC1D23\",85\r\n", 0), CAMERA_FRAME_PLATE);
	zassert_equal(frame.confidence, 85U);
	zassert_equal(feed_text("ERROR\r\n", 0), CAMERA_FRAME_ERROR);
}

ZTEST(camera_parser, test_partial_line_completes_before_deadline)
{
	zassert_equal(feed_text("+PLATE:1,", 0), CAMERA_FRAME_NONE);
	zassert_equal(feed_text("\"ABC1D23\",", 500), CAMERA_FRAME_NONE);
	zassert_equal(feed_text("93\r\n", 999), CAMERA_FRAME_PLATE);
}

ZTEST(camera_parser, test_partial_line_expires_1000_ms_after_first_byte)
{
	/* o prazo conta do PRIMEIRO byte: o último byte chega só 400 ms depois do anterior */
	zassert_equal(feed_text("+PLATE:1,", 0), CAMERA_FRAME_NONE);
	zassert_equal(feed_text("\"ABC1D23\",", 600), CAMERA_FRAME_NONE);
	zassert_equal(feed_text("93\r\n", 1000), CAMERA_FRAME_INVALID);
	/* e a linha seguinte, inteira, volta a funcionar */
	zassert_equal(feed_text("OK\r\n", 1010), CAMERA_FRAME_OK);
}

ZTEST(camera_parser, test_explicit_expire_and_idle_parser)
{
	/* parser ocioso não expira nada, por mais tempo que passe */
	camera_parser_expire(&parser, 100000);
	zassert_equal(feed_text("OK\r\n", 100000), CAMERA_FRAME_OK);

	/* expirar explicitamente uma linha parcial manda descartar até o LF */
	zassert_equal(feed_text("OK", 200000), CAMERA_FRAME_NONE);
	camera_parser_expire(&parser, 200999);
	zassert_equal(parser.len, 2U);
	camera_parser_expire(&parser, 201000);
	zassert_equal(parser.len, 0U);
	zassert_true(parser.discarding);
	zassert_equal(feed_text("\r\n", 201001), CAMERA_FRAME_INVALID);
}
