/**
 * @file test_hw.c
 * @brief Testes da camada de hardware (hw.c) com os drivers e modelos reais.
 *
 * A suíte roda na mesma máquina do Renode que o firmware: a cancela começa
 * fechada e o veículo longe (6000 mm).
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "hw.h"

/** @brief Inicia o hardware e espera a primeira amostra dos sensores. */
static void *setup(void)
{
	zassert_ok(hw_init());
	k_msleep(30);
	return NULL;
}

ZTEST_SUITE(hw, NULL, setup, NULL, NULL, NULL);

ZTEST(hw, test_outputs_accept_safe_levels)
{
	for (int lane = 0; lane < HW_LANE_COUNT; lane++) {
		for (int out = 0; out < HW_OUT_COUNT; out++) {
			zassert_ok(hw_output_set(lane, out, out == HW_OUT_RED));
		}
	}
}

ZTEST(hw, test_inputs_report_closed_barrier)
{
	for (int lane = 0; lane < HW_LANE_COUNT; lane++) {
		zassert_equal(hw_input_get(lane, HW_IN_LIMIT_LOWER), 1);
		zassert_equal(hw_input_get(lane, HW_IN_LIMIT_UPPER), 0);
		zassert_equal(hw_input_get(lane, HW_IN_BEAM), 0);
	}
}

ZTEST(hw, test_camera_answers_at_command)
{
	static const uint8_t at[] = "AT\r\n";
	char reply[8] = {0};
	size_t len = 0;
	uint8_t byte;

	zassert_equal(hw_uart_read(HW_UART_CAMERA_ENTRY, &byte), -EAGAIN);
	zassert_ok(hw_uart_write(HW_UART_CAMERA_ENTRY, at, sizeof(at) - 1));
	for (int ms = 0; ms < 200 && len < 4; ms++) {
		if (hw_uart_read(HW_UART_CAMERA_ENTRY, &byte) == 0) {
			reply[len++] = (char)byte;
		} else {
			k_msleep(1);
		}
	}
	zassert_str_equal(reply, "OK\r\n");
}

ZTEST(hw, test_adc_converts_both_lanes)
{
	for (int lane = 0; lane < HW_LANE_COUNT; lane++) {
		uint16_t raw = 0;
		int err;

		zassert_ok(hw_adc_start(lane));
		for (int ms = 0; ms < 100; ms++) {
			err = hw_adc_poll(&raw);
			if (err != -EAGAIN) {
				break;
			}
			k_msleep(1);
		}
		zassert_ok(err);
		zassert_equal(raw, 4095, "faixa %d: %u", lane, raw);
	}
}

ZTEST(hw, test_display_primitives)
{
	static const uint8_t colmod = 0x55;
	static const uint8_t pixel[] = {0xF8, 0x00};

	zassert_ok(hw_display_reset(true));
	k_msleep(10);
	zassert_ok(hw_display_reset(false));
	zassert_ok(hw_display_cmd(0x01, NULL, 0));
	zassert_ok(hw_display_cmd(0x3A, &colmod, 1));
	zassert_ok(hw_display_data(pixel, sizeof(pixel)));
}
