/* Tests of services/camera_proto: AT command and reply syntax. */
#include <errno.h>

#include <zephyr/ztest.h>

#include "services/camera_proto.h"

ZTEST_SUITE(camera_proto, NULL, NULL, NULL, NULL, NULL);

static struct camera_reply parse(const char *line)
{
	struct camera_reply r;

	camera_proto_parse(line, &r);
	return r;
}

ZTEST(camera_proto, test_format_capture)
{
	char out[32];

	zassert_equal(camera_proto_format_capture(1, out, sizeof(out)), 14);
	zassert_str_equal(out, "AT+CAPTURE=1\r\n");
	zassert_equal(camera_proto_format_capture(999999999u, out, sizeof(out)), 22);
	zassert_str_equal(out, "AT+CAPTURE=999999999\r\n");
}

ZTEST(camera_proto, test_format_capture_rejects_bad_id_and_small_buffer)
{
	char out[32];

	zassert_equal(camera_proto_format_capture(0, out, sizeof(out)), -EINVAL);
	zassert_equal(camera_proto_format_capture(1000000000u, out, sizeof(out)), -EINVAL);
	zassert_equal(camera_proto_format_capture(1, out, 5), -ENOSPC);
}

ZTEST(camera_proto, test_ok_and_error)
{
	zassert_equal(parse("OK").kind, CAMERA_REPLY_OK);
	zassert_equal(parse("ERROR").kind, CAMERA_REPLY_ERROR);
}

ZTEST(camera_proto, test_plate_reply_fields)
{
	struct camera_reply r = parse("+PLATE:12,\"ABC1D23\",93");

	zassert_equal(r.kind, CAMERA_REPLY_PLATE);
	zassert_equal(r.capture_id, 12);
	zassert_str_equal(r.plate, "ABC1D23");
	zassert_equal(r.confidence, 93);
	zassert_equal(parse("+PLATE:1,\"ABC1234\",0").confidence, 0);
	zassert_equal(parse("+PLATE:1,\"ABC1234\",100").confidence, 100);
}

ZTEST(camera_proto, test_malformed_replies_are_invalid)
{
	const char *bad[] = {
		"", "ok", "OK ", "Error", "AT", "+PLATE", "+PLATE:",
		"+PLATE:0,\"ABC1D23\",93",          /* id 0 */
		"+PLATE:01,\"ABC1D23\",93",         /* leading zero */
		"+PLATE:1000000000,\"ABC1D23\",93", /* id too large */
		"+PLATE:x,\"ABC1D23\",93",
		"+PLATE:1,\"ABC1D23\",101",         /* confidence > 100 */
		"+PLATE:1,\"ABC1D23\",085",
		"+PLATE:1,\"ABC1D23\",",
		"+PLATE:1,\"abc1d23\",93",          /* lowercase */
		"+PLATE:1,ABC1D23,93",              /* no quotes */
		"+PLATE:1,\"ABC 1D2\",93",
		"+PLATE:1,\"ABC1D23\",93,",         /* trailing text */
		"+PLATE:1,\"ABC1D23\"",
		"+PLATE:1,\"ABC1D2\",93",           /* short plate */
		"+PLATE:1 \"ABC1D23\",93",
		"+PLATE:1",
	};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_equal(parse(bad[i]).kind, CAMERA_REPLY_INVALID, "%s", bad[i]);
	}
}
