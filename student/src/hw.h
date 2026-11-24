/*
 * Camada fina de hardware: a única parte do firmware que chama os drivers
 * do Zephyr. A lógica (cancela, faixa, protocolos) só usa estas funções.
 * Nos testes de unidade elas são substituídas por falsos do FFF.
 */
#ifndef PARKING_HW_H
#define PARKING_HW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Faixas do estacionamento. Servem de índice nos vetores. */
enum lane_id {
	LANE_ENTRY = 0,
	LANE_EXIT = 1,
	LANE_COUNT
};

/** Saídas digitais de uma faixa (todas ativas em nível alto). */
enum hw_out {
	HW_OUT_ENABLE = 0, /* motor gira */
	HW_OUT_DIRECTION,  /* 1 abre, 0 fecha */
	HW_OUT_RED,
	HW_OUT_GREEN,
	HW_OUT_BUZZER,
	HW_OUT_COUNT
};

/** Entradas digitais de uma faixa (ativas em nível alto, com EXTI). */
enum hw_in {
	HW_IN_UPPER = 0, /* fim de curso superior: braço em 90° */
	HW_IN_LOWER,     /* fim de curso inferior: braço em 0° */
	HW_IN_BEAM,      /* feixe de passagem: 1 = algo no feixe */
	HW_IN_COUNT
};

/**
 * Função chamada em contexto de work (fila do sistema) sempre que uma
 * entrada da faixa muda. É ali que o motor é parado com urgência.
 */
typedef void (*hw_input_handler_t)(enum lane_id lane);

/**
 * Configura saídas (inativas), entradas e interrupções.
 * @return 0 ou -errno se algum dispositivo não estiver pronto.
 */
int hw_init(hw_input_handler_t handler);

/**
 * Agenda a work de entrada da faixa para daqui a ms (prazo do motor). Ela
 * chama o mesmo handler da EXTI, que confere o prazo e para o motor sem
 * depender do laço principal.
 */
void hw_wake_after(enum lane_id lane, int32_t ms);

/** Escreve uma saída. @return 0 ou -errno do driver. */
int hw_out_set(enum lane_id lane, enum hw_out out, bool on);

/** Lê uma entrada. @return 0, 1 ou -errno do driver. */
int hw_in_get(enum lane_id lane, enum hw_in in);

/**
 * Dispara uma conversão assíncrona do ADC4 no canal da faixa.
 * Buffer, sequência e sinal são estáticos em hw.c: sobrevivem a uma
 * conclusão atrasada. Só chame com nenhuma conversão pendente.
 * @return 0 ou -errno de adc_read_async.
 */
int hw_adc_start(enum lane_id lane);

/**
 * Confere, sem bloquear, a conversão pendente.
 * @return 0 com *raw preenchido, -EINPROGRESS se ainda não terminou,
 *         ou o erro (-errno) informado pelo driver na conclusão.
 */
int hw_adc_poll(uint16_t *raw);

/** UARTs dos equipamentos. */
enum hw_uart {
	HW_UART_CAMERA_ENTRY = 0, /* USART3 */
	HW_UART_CAMERA_EXIT,      /* USART2 */
	HW_UART_PAYMENT,          /* LPUART1 */
	HW_UART_COUNT
};

/**
 * Envia bytes (por polling: quadros curtos, no máximo 128 bytes).
 * @return 0 ou -errno.
 */
int hw_uart_write(enum hw_uart uart, const char *data, size_t len);

/**
 * Retira até max bytes já recebidos pela interrupção. Não bloqueia.
 * @return quantos bytes foram copiados (0 se não havia nada).
 */
size_t hw_uart_read(enum hw_uart uart, uint8_t *buf, size_t max);

/** Reset por hardware do display: true = em reset (pino em nível baixo). */
int hw_display_reset(bool asserted);

/**
 * Uma transação SPI com o display (CS ativo durante ela).
 * @param data false = bytes de comando (D/C baixo), true = dados (D/C alto).
 * @return 0 ou -errno.
 */
int hw_display_write(bool data, uint8_t *buf, size_t len);

/** Espera bloqueante; usada só na inicialização do display, no boot. */
void hw_sleep_ms(int32_t ms);

/** Valor que muda com o tempo (ciclos da CPU), para variar os UUIDs. */
uint32_t hw_entropy(void);

/** Tempo virtual desde o boot, em ms. */
int64_t hw_now_ms(void);

/*
 * Impedem a troca de thread (a work de parada não roda) entre as duas
 * chamadas. Usado só para "marcar o movimento e ligar o motor" juntos.
 */
void hw_sched_lock(void);
void hw_sched_unlock(void);

#endif /* PARKING_HW_H */
