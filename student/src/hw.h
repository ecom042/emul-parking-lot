/*
 * Camada fina de hardware: a ÚNICA parte do firmware que chama drivers do
 * Zephyr (GPIO/EXTI, ADC4, UART, SPI). Todo o resto (lane.c, adc_sampler.c,
 * display.c...) só chama estas funções.
 *
 * Por que separar: nos testes de unidade (student/tests/) estas funções são
 * substituídas por falsos do FFF. Assim a lógica é testada sem Renode, e os
 * falsos podem devolver erros (-EIO...) que os drivers reais nunca dão no
 * laboratório, cobrindo os caminhos de erro.
 */
#ifndef HW_H_
#define HW_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parking.h"

/** Comando do motor da cancela (enable + direction). */
enum motor_cmd {
	MOTOR_OFF,   /* enable baixo; direção mantida */
	MOTOR_OPEN,  /* direção alta (sobe), depois enable alto */
	MOTOR_CLOSE, /* direção baixa (desce), depois enable alto */
};

/** UARTs de equipamento (a USART1 é o console e fica de fora). */
enum uart_ch {
	UART_CAM_ENTRY = 0, /* USART3 */
	UART_CAM_EXIT = 1,  /* USART2 */
	UART_PAYMENT = 2,   /* LPUART1 */
	UART_COUNT = 3,
};

/** Configura todos os periféricos. Saídas: motor/verde/buzzer baixos, vermelho alto. */
int hw_init(void);

/* ---- GPIO --------------------------------------------------------------- */
int hw_motor(enum lane_id lane, enum motor_cmd cmd);
int hw_lights(enum lane_id lane, bool red, bool green);
int hw_buzzer(enum lane_id lane, bool on);
bool hw_limit_upper(enum lane_id lane);
bool hw_limit_lower(enum lane_id lane);
bool hw_beam(enum lane_id lane);

/* ---- UART --------------------------------------------------------------- */
/** Envia os bytes (bloqueia só o tempo de escrevê-los no registrador). */
int hw_uart_send(enum uart_ch ch, const char *buf, size_t len);
/** Tira até max bytes recebidos pela interrupção. @return quantos. */
size_t hw_uart_read(enum uart_ch ch, uint8_t *buf, size_t max);

/* ---- ADC4 assíncrono ---------------------------------------------------- */
/** Dispara UMA conversão no canal da faixa (17 entrada, 18 saída). */
int hw_adc_start(enum lane_id lane);
/**
 * Verifica a conversão em andamento SEM bloquear.
 * @return 0 e *raw preenchido; -EINPROGRESS se ainda não terminou; ou o
 *         erro devolvido pelo driver.
 */
int hw_adc_check(uint16_t *raw);

/* ---- Display ST7789V (SPI1 + D/C + reset) ------------------------------- */
/** Nível do reset do display (true = reset ativo). */
int hw_disp_reset(bool asserted);
/** Uma transação SPI: data=false para comando (D/C baixo), true para dados. */
int hw_disp_write(bool data, uint8_t *buf, size_t len);

/* ---- Tempo e espera ----------------------------------------------------- */
/** Tempo virtual desde o boot, em ms (k_uptime_get no Renode). */
int64_t hw_now_ms(void);
/** Dorme a thread atual (usado só pela thread do display). */
void hw_sleep_ms(int32_t ms);
/** Espera um evento (EXTI, UART, display) ou o tempo máximo. */
void hw_wait_event(int32_t timeout_ms);
/** Acorda o laço de controle (chamado de ISRs e da thread do display). */
void hw_wake(void);

#endif /* HW_H_ */
