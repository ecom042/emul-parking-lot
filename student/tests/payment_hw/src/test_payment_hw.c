/*
 * test_payment_hw.c — teste do driver UART REAL do terminal (LPUART1), sem falsos.
 *
 * Truque seguro: mandamos "P1 CANCEL <uuid>" de um UUID que o terminal nunca viu.
 * Ele não cria nenhuma cobrança e responde "P1 ERROR UNKNOWN\n" (17 bytes) no
 * tick seguinte. Isso prova envio E recepção pela UART real.
 */
#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "payment_hw.h"

ZTEST_SUITE(payment_uart_hw, NULL, NULL, NULL, NULL, NULL);

ZTEST(payment_uart_hw, test_init_finds_the_uart)
{
	zassert_ok(payment_hw_init());
}

ZTEST(payment_uart_hw, test_unknown_cancel_is_answered_with_error_unknown)
{
	static const char request[] = "P1 CANCEL 00000000-0000-4000-8000-000000000000\n";
	static const char expected[] = "P1 ERROR UNKNOWN\n";
	uint8_t reply[sizeof(expected) - 1U] = { 0 };
	size_t got = 0U;

	zassert_ok(payment_hw_init());
	zassert_ok(payment_hw_send((const uint8_t *)request, sizeof(request) - 1U));
	for (int i = 0; i < 40 && got < sizeof(reply); ++i) {
		int count;

		k_sleep(K_MSEC(5));
		count = payment_hw_read(&reply[got], sizeof(reply) - got);
		zassert_true(count >= 0);
		got += (size_t)count;
	}
	zassert_equal(got, sizeof(reply));
	zassert_mem_equal(reply, expected, sizeof(reply));
}

ZTEST(payment_uart_hw, test_read_without_data_returns_zero)
{
	uint8_t byte;

	zassert_equal(payment_hw_read(&byte, sizeof(byte)), 0);
	zassert_equal(payment_hw_read(&byte, 0U), 0);
}

ZTEST(payment_uart_hw, test_null_buffers_are_rejected)
{
	uint8_t byte = 0U;

	zassert_equal(payment_hw_send(NULL, 1U), -EINVAL);
	zassert_equal(payment_hw_read(NULL, 1U), -EINVAL);
	zassert_ok(payment_hw_send(&byte, 0U)); /* enviar 0 bytes é válido */
}
