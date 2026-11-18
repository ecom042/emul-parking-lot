/*
 * test_camera_hw.c — teste do driver UART REAL das duas câmeras.
 *
 * Aqui NÃO há falsos: camera_hw.c usa uart_poll_out/uart_poll_in de verdade e
 * o modelo da câmera no Renode responde. O comando "AT\r\n" não tira foto; só
 * confere a comunicação, e o modelo devolve "OK\r\n" no tick seguinte (10 ms).
 */
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "camera_hw.h"

ZTEST_SUITE(camera_uart_hw, NULL, NULL, NULL, NULL, NULL);

/* Envia "AT" à câmera da faixa e espera (até 200 ms virtuais) pelos 4 bytes "OK\r\n". */
static void expect_ok_reply(unsigned int lane)
{
	static const uint8_t at[] = { 'A', 'T', '\r', '\n' };
	static const uint8_t expected[] = { 'O', 'K', '\r', '\n' };
	uint8_t reply[sizeof(expected)] = { 0 };
	size_t got = 0U;

	zassert_ok(camera_hw_send(lane, at, sizeof(at)));
	for (int i = 0; i < 40 && got < sizeof(reply); ++i) {
		int count;

		k_sleep(K_MSEC(5));
		count = camera_hw_read(lane, &reply[got], sizeof(reply) - got);
		zassert_true(count >= 0);
		got += (size_t)count;
	}
	zassert_equal(got, sizeof(expected));
	zassert_mem_equal(reply, expected, sizeof(expected));
}

ZTEST(camera_uart_hw, test_init_finds_both_uarts)
{
	zassert_ok(camera_hw_init());
}

ZTEST(camera_uart_hw, test_entry_camera_answers_at_with_ok)
{
	zassert_ok(camera_hw_init());
	expect_ok_reply(0U);
}

ZTEST(camera_uart_hw, test_exit_camera_answers_at_with_ok)
{
	zassert_ok(camera_hw_init());
	expect_ok_reply(1U);
}

ZTEST(camera_uart_hw, test_read_without_data_returns_zero)
{
	uint8_t byte;

	zassert_equal(camera_hw_read(0U, &byte, sizeof(byte)), 0);
	zassert_equal(camera_hw_read(0U, &byte, 0U), 0);
}

ZTEST(camera_uart_hw, test_invalid_arguments_are_rejected)
{
	static const uint8_t at[] = { 'A', 'T' };
	uint8_t byte;

	zassert_equal(camera_hw_send(2U, at, sizeof(at)), -EINVAL);
	zassert_equal(camera_hw_send(0U, NULL, 1U), -EINVAL);
	zassert_equal(camera_hw_read(2U, &byte, 1U), -EINVAL);
	zassert_equal(camera_hw_read(0U, NULL, 1U), -EINVAL);
}
