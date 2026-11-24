/*
 * Camada de hardware falsa (FFF) compartilhada pelas suítes.
 *
 * As entradas vêm de fake_in[][]; as saídas escritas ficam em fake_out[][].
 * Cada falso pode devolver erro via <função>_fake.return_val ou
 * custom_fake, como em qualquer falso do FFF.
 */
#ifndef PARKING_HW_FAKE_H
#define PARKING_HW_FAKE_H

#include <zephyr/fff.h>

#include "hw.h"

DECLARE_FAKE_VALUE_FUNC(int, hw_init, hw_input_handler_t);
DECLARE_FAKE_VALUE_FUNC(int, hw_out_set, enum lane_id, enum hw_out, bool);
DECLARE_FAKE_VALUE_FUNC(int, hw_in_get, enum lane_id, enum hw_in);
DECLARE_FAKE_VALUE_FUNC(int64_t, hw_now_ms);
DECLARE_FAKE_VALUE_FUNC(int, hw_adc_start, enum lane_id);
DECLARE_FAKE_VALUE_FUNC(int, hw_adc_poll, uint16_t *);
DECLARE_FAKE_VALUE_FUNC(int, hw_uart_write, enum hw_uart, const char *, size_t);
DECLARE_FAKE_VALUE_FUNC(size_t, hw_uart_read, enum hw_uart, uint8_t *, size_t);
DECLARE_FAKE_VOID_FUNC(hw_wake_after, enum lane_id, int32_t);
DECLARE_FAKE_VALUE_FUNC(int, hw_display_reset, bool);
DECLARE_FAKE_VALUE_FUNC(int, hw_display_write, bool, uint8_t *, size_t);
DECLARE_FAKE_VOID_FUNC(hw_sleep_ms, int32_t);
DECLARE_FAKE_VALUE_FUNC(uint32_t, hw_entropy);
DECLARE_FAKE_VOID_FUNC(hw_sched_lock);
DECLARE_FAKE_VOID_FUNC(hw_sched_unlock);

/* Nível de cada entrada simulada (0/1) ou -errno para simular erro. */
extern int fake_in[LANE_COUNT][HW_IN_COUNT];
/* Último valor escrito em cada saída. */
extern bool fake_out[LANE_COUNT][HW_OUT_COUNT];
/* Erro (-errno) a devolver ao escrever cada saída; 0 = sucesso. */
extern int fake_out_err[LANE_COUNT][HW_OUT_COUNT];
/* Quantas vezes cada saída foi escrita com 1. */
extern int fake_out_on_count[LANE_COUNT][HW_OUT_COUNT];

/* Bytes "recebidos" que hw_uart_read entrega, por UART. */
void fake_uart_rx(enum hw_uart uart, const char *data);
/* Tudo o que foi escrito em cada UART desde o reset (texto). */
extern char fake_uart_tx[HW_UART_COUNT][512];

/** Zera os falsos e deixa as duas cancelas fechadas (inferior = 1). */
void hw_fake_reset(void);

#endif /* PARKING_HW_FAKE_H */
