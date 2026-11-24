/*
 * Testes do protocolo da câmera (camera.c, REQ-02/11).
 */
#include <errno.h>

#include <zephyr/ztest.h>

#include "camera.h"

ZTEST_SUITE(camera, NULL, NULL, NULL, NULL, NULL);

ZTEST(camera, test_format_capture)
{
	char buf[32];

	zassert_equal(camera_format_capture(buf, sizeof(buf), 17), 15);
	zassert_str_equal(buf, "AT+CAPTURE=17\r\n");
	zassert_equal(camera_format_capture(buf, sizeof(buf), 999999999), 22);
	zassert_equal(camera_format_capture(buf, sizeof(buf), 0), -EINVAL);
	zassert_equal(camera_format_capture(buf, sizeof(buf), 1000000000), -EINVAL);
	zassert_equal(camera_format_capture(buf, 8, 17), -EINVAL, "buffer pequeno");
}

ZTEST(camera, test_ok_and_error)
{
	struct camera_msg m;

	camera_parse("OK", &m);
	zassert_equal(m.kind, CAMERA_OK);
	camera_parse("ERROR", &m);
	zassert_equal(m.kind, CAMERA_ERROR);
	camera_parse("OK ", &m);
	zassert_equal(m.kind, CAMERA_INVALID);
	camera_parse("ok", &m);
	zassert_equal(m.kind, CAMERA_INVALID);
}

ZTEST(camera, test_plate_valid)
{
	struct camera_msg m;

	camera_parse("+PLATE:17,\"ABC1D23\",93", &m);
	zassert_equal(m.kind, CAMERA_PLATE);
	zassert_equal(m.id, 17);
	zassert_str_equal(m.plate, "ABC1D23");
	zassert_equal(m.confidence, 93);

	camera_parse("+PLATE:999999999,\"ABC1234\",0", &m);
	zassert_equal(m.kind, CAMERA_PLATE);
	zassert_str_equal(m.plate, "ABC1234");
	zassert_equal(m.confidence, 0);

	camera_parse("+PLATE:1,\"XYZ9Z99\",100", &m);
	zassert_equal(m.kind, CAMERA_PLATE);
}

ZTEST(camera, test_plate_malformed)
{
	static const char *const bad[] = {
		"+PLATE:017,\"ABC1D23\",93",   /* zero à esquerda no ID */
		"+PLATE:0,\"ABC1D23\",93",     /* ID 0 */
		"+PLATE:1000000000,\"ABC1D23\",93",
		"+PLATE:17,\"ABC1D23\",101",   /* confiança > 100 */
		"+PLATE:17,\"ABC1D23\",093",
		"+PLATE:17,\"ABC1D23\",",      /* confiança vazia */
		"+PLATE:17,\"ABC1D23\",93 ",   /* espaço extra */
		"+PLATE:17, \"ABC1D23\",93",
		"+PLATE:17,ABC1D23,93",        /* sem aspas */
		"+PLATE:17,\"ABC1D23,93",
		"+PLATE:17,\"ABC1D2\",93",     /* placa curta */
		"+PLATE:17,\"ABC1D234\",93",   /* placa longa */
		"+PLATE:17,\"abc1d23\",93",    /* minúsculas */
		"+PLATE:17,\"AB11D23\",93",    /* 3ª posição não é letra */
		"+PLATE:17,\"ABCDD23\",93",    /* 4ª posição não é dígito */
		"+PLATE:17,\"ABC1-23\",93",    /* 5ª posição inválida */
		"+PLATE:17,\"ABC1DX3\",93",
		"+PLATE:17,\"ABC1D2X\",93",
		"+PLATE:17\"ABC1D23\",93",
		"+PLATE:17,\"ABC1D23\"93",
		"+PLATE:,\"ABC1D23\",93",
		"PLATE:17,\"ABC1D23\",93",
		"+PLATE:17,\"ABC1D23\",93,1",
		"+PLATE:17,\"\",93",
	};
	struct camera_msg m;

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		camera_parse(bad[i], &m);
		zassert_equal(m.kind, CAMERA_INVALID, "aceitou: %s", bad[i]);
	}
}

ZTEST(camera, test_plate_regex)
{
	zassert_true(camera_plate_valid("ABC1D23"));
	zassert_true(camera_plate_valid("ABC1234"));
	zassert_false(camera_plate_valid("ABC123"));
	zassert_false(camera_plate_valid("A1C1D23"));
	zassert_false(camera_plate_valid("AB$1D23"));
	zassert_false(camera_plate_valid("ABC1D2a"));
}
