/* Implementação dos falsos de hw.h (ver hw_fake.h). */
#include "hw_fake.h"

#include <string.h>

#include <zephyr/sys/util.h>

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(int, hw_init, hw_input_handler_t);
DEFINE_FAKE_VALUE_FUNC(int, hw_out_set, enum lane_id, enum hw_out, bool);
DEFINE_FAKE_VALUE_FUNC(int, hw_in_get, enum lane_id, enum hw_in);
DEFINE_FAKE_VALUE_FUNC(int64_t, hw_now_ms);
DEFINE_FAKE_VALUE_FUNC(int, hw_adc_start, enum lane_id);
DEFINE_FAKE_VALUE_FUNC(int, hw_adc_poll, uint16_t *);
DEFINE_FAKE_VALUE_FUNC(int, hw_uart_write, enum hw_uart, const char *, size_t);
DEFINE_FAKE_VALUE_FUNC(size_t, hw_uart_read, enum hw_uart, uint8_t *, size_t);
DEFINE_FAKE_VOID_FUNC(hw_wake_after, enum lane_id, int32_t);
DEFINE_FAKE_VALUE_FUNC(int, hw_display_reset, bool);
DEFINE_FAKE_VALUE_FUNC(int, hw_display_write, bool, uint8_t *, size_t);
DEFINE_FAKE_VOID_FUNC(hw_sleep_ms, int32_t);
DEFINE_FAKE_VALUE_FUNC(uint32_t, hw_entropy);
DEFINE_FAKE_VOID_FUNC(hw_sched_lock);
DEFINE_FAKE_VOID_FUNC(hw_sched_unlock);

int fake_in[LANE_COUNT][HW_IN_COUNT];
bool fake_out[LANE_COUNT][HW_OUT_COUNT];
int fake_out_on_count[LANE_COUNT][HW_OUT_COUNT];
int fake_out_err[LANE_COUNT][HW_OUT_COUNT];

/* Registra a escrita, a menos que o teste tenha pedido erro nessa saída. */
static int out_set_custom(enum lane_id lane, enum hw_out out, bool on)
{
	if (fake_out_err[lane][out] != 0) {
		return fake_out_err[lane][out];
	}
	fake_out[lane][out] = on;
	if (on) {
		fake_out_on_count[lane][out]++;
	}
	return 0;
}

char fake_uart_tx[HW_UART_COUNT][512];
static char rx_data[HW_UART_COUNT][512];
static size_t rx_len[HW_UART_COUNT];
static size_t rx_pos[HW_UART_COUNT];

void fake_uart_rx(enum hw_uart uart, const char *data)
{
	size_t n = strlen(data);

	/* Compacta o que já foi lido e acrescenta os novos bytes. */
	memmove(rx_data[uart], rx_data[uart] + rx_pos[uart], rx_len[uart] - rx_pos[uart]);
	rx_len[uart] -= rx_pos[uart];
	rx_pos[uart] = 0;
	memcpy(rx_data[uart] + rx_len[uart], data, n);
	rx_len[uart] += n;
}

static size_t uart_read_custom(enum hw_uart uart, uint8_t *buf, size_t max)
{
	size_t n = MIN(max, rx_len[uart] - rx_pos[uart]);

	memcpy(buf, rx_data[uart] + rx_pos[uart], n);
	rx_pos[uart] += n;
	return n;
}

static int uart_write_custom(enum hw_uart uart, const char *data, size_t len)
{
	size_t used = strlen(fake_uart_tx[uart]);

	if (hw_uart_write_fake.return_val != 0) {
		return hw_uart_write_fake.return_val;
	}
	if (used + len < sizeof(fake_uart_tx[uart])) {
		memcpy(fake_uart_tx[uart] + used, data, len);
		fake_uart_tx[uart][used + len] = '\0';
	}
	return 0;
}

static int in_get_custom(enum lane_id lane, enum hw_in in)
{
	return fake_in[lane][in];
}

void hw_fake_reset(void)
{
	RESET_FAKE(hw_init);
	RESET_FAKE(hw_out_set);
	RESET_FAKE(hw_in_get);
	RESET_FAKE(hw_now_ms);
	RESET_FAKE(hw_adc_start);
	RESET_FAKE(hw_adc_poll);
	RESET_FAKE(hw_uart_write);
	RESET_FAKE(hw_uart_read);
	RESET_FAKE(hw_wake_after);
	RESET_FAKE(hw_display_reset);
	RESET_FAKE(hw_display_write);
	RESET_FAKE(hw_sleep_ms);
	RESET_FAKE(hw_entropy);
	RESET_FAKE(hw_sched_lock);
	RESET_FAKE(hw_sched_unlock);
	FFF_RESET_HISTORY();

	memset(fake_in, 0, sizeof(fake_in));
	memset(fake_out, 0, sizeof(fake_out));
	memset(fake_out_on_count, 0, sizeof(fake_out_on_count));
	memset(fake_out_err, 0, sizeof(fake_out_err));
	for (int lane = 0; lane < LANE_COUNT; lane++) {
		fake_in[lane][HW_IN_LOWER] = 1;
	}
	memset(fake_uart_tx, 0, sizeof(fake_uart_tx));
	memset(rx_len, 0, sizeof(rx_len));
	memset(rx_pos, 0, sizeof(rx_pos));
	hw_uart_write_fake.custom_fake = uart_write_custom;
	hw_uart_read_fake.custom_fake = uart_read_custom;
	hw_out_set_fake.custom_fake = out_set_custom;
	hw_in_get_fake.custom_fake = in_get_custom;
}
