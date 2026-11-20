#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "camera.h"

static struct camera_parser parser;
static struct camera_plate plate;

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	camera_parser_reset(&parser);
	plate = (struct camera_plate){0};
}

/* Feeds every byte; returns how many complete results were produced. */
static int feed(const char *bytes, size_t len)
{
	int results = 0;

	for (size_t i = 0; i < len; i++) {
		results += camera_parser_feed(&parser, (uint8_t)bytes[i], &plate) ? 1 : 0;
	}
	return results;
}

static int feed_str(const char *bytes)
{
	return feed(bytes, strlen(bytes));
}

ZTEST(camera, test_plate_syntax)
{
	zassert_true(camera_plate_valid("ABC1D23", 7));
	zassert_true(camera_plate_valid("ABC1234", 7));
	zassert_false(camera_plate_valid("ABC1D2", 6));
	zassert_false(camera_plate_valid("ABC1D234", 8));
	zassert_false(camera_plate_valid("abc1D23", 7));
	zassert_false(camera_plate_valid("AB11D23", 7));
	zassert_false(camera_plate_valid("ABCDD23", 7));
	zassert_false(camera_plate_valid("ABC1-23", 7));
	zassert_false(camera_plate_valid("ABC1DA3", 7));
	zassert_false(camera_plate_valid("ABC1D2A", 7));
}

ZTEST(camera, test_complete_frame)
{
	zassert_equal(feed_str("+PLATE:17,\"ABC1D23\",93\r\n"), 1);
	zassert_equal(plate.id, 17);
	zassert_equal(plate.confidence, 93);
	zassert_str_equal(plate.plate, "ABC1D23");
}

ZTEST(camera, test_fragmented_after_ok)
{
	const char *chunks[] = { "OK\r\n", "+PLATE:1", "23,\"XYZ9", "A00\",85\r", "\n" };
	int results = 0;

	for (size_t i = 0; i < ARRAY_SIZE(chunks); i++) {
		results += feed_str(chunks[i]);
		zassert_equal(results, i + 1 == ARRAY_SIZE(chunks) ? 1 : 0, "chunk %zu", i);
	}
	zassert_equal(plate.id, 123);
	zassert_equal(plate.confidence, 85);
	zassert_str_equal(plate.plate, "XYZ9A00");
}

ZTEST(camera, test_ok_and_error_are_not_results)
{
	zassert_equal(feed_str("OK\r\nERROR\r\nAT\r\n"), 0);
}

ZTEST(camera, test_bounds)
{
	zassert_equal(feed_str("+PLATE:999999999,\"ABC1D23\",100\r\n"), 1);
	zassert_equal(plate.id, CAMERA_ID_MAX);
	zassert_equal(plate.confidence, 100);
	zassert_equal(feed_str("+PLATE:5,\"ABC1D23\",0\r\n"), 1);
	zassert_equal(plate.confidence, 0);
}

ZTEST(camera, test_malformed_frames_are_ignored)
{
	const char *bad[] = {
		"+PLATE:0,\"ABC1D23\",93\r\n",		/* ID zero */
		"+PLATE:01,\"ABC1D23\",93\r\n",		/* leading zero */
		"+PLATE:1000000000,\"ABC1D23\",93\r\n",	/* ID too large */
		"+PLATE:99999999999,\"ABC1D23\",93\r\n",	/* overflow guard */
		"+PLATE:1,\"ABC1D23\",101\r\n",		/* confidence > 100 */
		"+PLATE:1,\"ABC1D23\",093\r\n",		/* leading zero */
		"+PLATE:1,\"ABC1D23\",\r\n",		/* empty field */
		"+PLATE:1,\"abc1d23\",93\r\n",		/* plate syntax */
		"+PLATE:1,\"ABC1D23,93\r\n",		/* missing quote */
		"+PLATE:1,ABC1D23,93\r\n",		/* no quotes */
		"+PLATE:1 ,\"ABC1D23\",93\r\n",		/* whitespace */
		"+PLATE:1,\"ABC1D23\",93 \r\n",		/* trailing space */
		"+PLATE:1,\"ABC1D23\"93\r\n",		/* missing comma */
		"+PLATE:1\"ABC1D23\",93\r\n",		/* missing comma */
		"+PLATE:,\"ABC1D23\",93\r\n",		/* empty ID */
		"+PLATE:\r\n",				/* prefix only */
		"+plate:1,\"ABC1D23\",93\r\n",		/* case-sensitive */
		"+PLATE:1,\"ABC1D23\",93\n",		/* LF without CR */
		"+PLATE:1,\"ABC1D23\",93\r\r\n",	/* stray CR */
		"+PLATE:1,\"ABC1D23\"\r,93\r\n",	/* CR inside the body */
		"\r\n",					/* empty frame */
	};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_equal(feed_str(bad[i]), 0, "frame %zu accepted", i);
	}
	/* The parser resynchronizes on the next good frame. */
	zassert_equal(feed_str("+PLATE:2,\"ABC1D23\",93\r\n"), 1);
	zassert_equal(plate.id, 2);
}

ZTEST(camera, test_non_printable_byte_discards_frame)
{
	const char frame[] = "+PLATE:1,\"ABC\x01" "1D23\",93\r\n";

	zassert_equal(feed(frame, sizeof(frame) - 1), 0);
	zassert_equal(feed_str("+PLATE:3,\"ABC1D23\",93\r\n"), 1);
}

ZTEST(camera, test_oversized_frame_is_discarded)
{
	char line[CAMERA_FRAME_MAX + 8];
	const char *head = "+PLATE:1,\"ABC1D23\",93";
	size_t len = strlen(head);

	/* 127 body bytes + CRLF = 129 bytes, one beyond the limit. */
	memcpy(line, head, len);
	memset(line + len, '0', 127 - len);
	memcpy(line + 127, "\r\n", 2);
	zassert_equal(feed(line, 129), 0);
	zassert_equal(feed_str("+PLATE:4,\"ABC1D23\",93\r\n"), 1);
	zassert_equal(plate.id, 4);
}

ZTEST(camera, test_frame_at_limit_is_parsed_up_to_syntax)
{
	char line[CAMERA_FRAME_MAX];

	/* 126 body bytes + CRLF = 128: retained, then rejected only by syntax. */
	memset(line, 'A', 126);
	memcpy(line + 126, "\r\n", 2);
	zassert_equal(feed(line, sizeof(line)), 0);
	zassert_false(parser.discard);
	zassert_equal(parser.len, 0);
}

ZTEST(camera, test_format_capture)
{
	char buf[32];

	zassert_equal(camera_format_capture(42, buf, sizeof(buf)), 15);
	zassert_str_equal(buf, "AT+CAPTURE=42\r\n");
	zassert_equal(camera_format_capture(CAMERA_ID_MAX, buf, sizeof(buf)), 22);
	zassert_equal(camera_format_capture(0, buf, sizeof(buf)), -EINVAL);
	zassert_equal(camera_format_capture(CAMERA_ID_MAX + 1, buf, sizeof(buf)), -EINVAL);
	zassert_equal(camera_format_capture(42, buf, 8), -EINVAL);
}

ZTEST_SUITE(camera, NULL, NULL, before_each, NULL, NULL);
