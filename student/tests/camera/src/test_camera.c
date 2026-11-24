/**
 * @file test_camera.c
 * @brief Testes do pedido de captura e da leitura do +PLATE (camera.c).
 */

#include <errno.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "camera.h"

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, hw_uart_write, enum hw_uart, const uint8_t *, size_t);

static char sent[32];

/** @brief Guarda o que seria transmitido. */
static int capture_write(enum hw_uart uart, const uint8_t *data, size_t len)
{
	ARG_UNUSED(uart);
	memcpy(sent, data, len);
	sent[len] = '\0';
	return 0;
}

static void reset_fakes(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(hw_uart_write);
	hw_uart_write_fake.custom_fake = capture_write;
	memset(sent, 0, sizeof(sent));
}

ZTEST_SUITE(camera, NULL, NULL, reset_fakes, NULL, NULL);

/** @brief Interpreta um quadro com a captura 7. */
static int parse(const char *frame, char *plate, unsigned int *confidence)
{
	return camera_parse_plate(frame, 7, plate, confidence);
}

ZTEST(camera, test_request_sends_capture_command)
{
	zassert_ok(camera_request(HW_UART_CAMERA_EXIT, 42));
	zassert_equal(hw_uart_write_fake.arg0_val, HW_UART_CAMERA_EXIT);
	zassert_str_equal(sent, "AT+CAPTURE=42\r\n");
}

ZTEST(camera, test_request_accepts_largest_id)
{
	zassert_ok(camera_request(HW_UART_CAMERA_ENTRY, CAMERA_MAX_ID));
	zassert_str_equal(sent, "AT+CAPTURE=999999999\r\n");
}

ZTEST(camera, test_request_rejects_out_of_range_ids)
{
	zassert_equal(camera_request(HW_UART_CAMERA_ENTRY, 0), -EINVAL);
	zassert_equal(camera_request(HW_UART_CAMERA_ENTRY, CAMERA_MAX_ID + 1U), -EINVAL);
	zassert_equal(hw_uart_write_fake.call_count, 0);
}

ZTEST(camera, test_request_returns_uart_error)
{
	hw_uart_write_fake.custom_fake = NULL;
	hw_uart_write_fake.return_val = -EIO;
	zassert_equal(camera_request(HW_UART_CAMERA_ENTRY, 1), -EIO);
}

ZTEST(camera, test_parse_valid_result)
{
	char plate[CAMERA_PLATE_LEN + 1];
	unsigned int confidence;

	zassert_ok(parse("+PLATE:7,\"ABC1D23\",93", plate, &confidence));
	zassert_str_equal(plate, "ABC1D23");
	zassert_equal(confidence, 93);
}

ZTEST(camera, test_parse_confidence_limits)
{
	char plate[CAMERA_PLATE_LEN + 1];
	unsigned int confidence;

	zassert_ok(parse("+PLATE:7,\"XYZ9876\",0", plate, &confidence));
	zassert_equal(confidence, 0);
	zassert_ok(parse("+PLATE:7,\"XYZ9876\",100", plate, &confidence));
	zassert_equal(confidence, 100);
}

ZTEST(camera, test_parse_rejects_malformed_frames)
{
	static const char *const bad[] = {
		"OK",
		"ERROR",
		"+PLATE7,\"ABC1D23\",93",
		"+PLATE:7\"ABC1D23\"93",
		"+PLATE:,\"ABC1D23\",93",
		"+PLATE:07,\"ABC1D23\",93",
		"+PLATE:7a,\"ABC1D23\",93",
		"+PLATE:8,\"ABC1D23\",93",
		"+PLATE:9999999999,\"ABC1D23\",93",
		"+PLATE:7,ABC1D23,93",
		"+PLATE:7,\"ABC1D2\",93",
		"+PLATE:7,\"ABC1D234\",93",
		"+PLATE:7,\"ABC1D23\"93",
		"+PLATE:7,\"aBC1D23\",93",
		"+PLATE:7,\"AbC1D23\",93",
		"+PLATE:7,\"ABc1D23\",93",
		"+PLATE:7,\"ABCDD23\",93",
		"+PLATE:7,\"ABC1-23\",93",
		"+PLATE:7,\"ABC1DA3\",93",
		"+PLATE:7,\"ABC1D2A\",93",
		"+PLATE:7,\"ABC1D23\",",
		"+PLATE:7,\"ABC1D23\",093",
		"+PLATE:7,\"ABC1D23\",101",
		"+PLATE:7,\"ABC1D23\",9 3",
		"+PLATE:7,\"ABC1D23\",93 ",
	};
	char plate[CAMERA_PLATE_LEN + 1];
	unsigned int confidence;

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_equal(parse(bad[i], plate, &confidence), -EINVAL, "%s", bad[i]);
	}
}
