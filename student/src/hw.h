/**
 * @file hw.h
 * @brief Camada fina de hardware: o único módulo que chama drivers do Zephyr.
 *
 * Os módulos de lógica (faixa, câmera, pagamento, display) só falam com o
 * hardware por estas funções. Nos testes de unidade elas são substituídas por
 * falsos do FFF, o que permite exercitar os caminhos de erro dos drivers.
 */

#ifndef HW_H_
#define HW_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Faixas do estacionamento. */
enum hw_lane {
	HW_LANE_ENTRY = 0, /**< Faixa de entrada. */
	HW_LANE_EXIT,      /**< Faixa de saída (tem terminal e display). */
	HW_LANE_COUNT,     /**< Quantidade de faixas. */
};

/** @brief Saídas digitais de uma faixa, todas ativas em nível alto. */
enum hw_output {
	HW_OUT_MOTOR_ENABLE = 0, /**< Motor da cancela ligado. */
	HW_OUT_MOTOR_DIRECTION,  /**< Alto abre, baixo fecha. */
	HW_OUT_RED,              /**< Sinal vermelho. */
	HW_OUT_GREEN,            /**< Sinal verde. */
	HW_OUT_BUZZER,           /**< Buzzer. */
	HW_OUT_COUNT,            /**< Quantidade de saídas. */
};

/** @brief Entradas digitais de uma faixa, todas ativas em nível alto. */
enum hw_input {
	HW_IN_LIMIT_UPPER = 0, /**< Fim de curso superior (cancela aberta). */
	HW_IN_LIMIT_LOWER,     /**< Fim de curso inferior (cancela fechada). */
	HW_IN_BEAM,            /**< Feixe de passagem interrompido. */
	HW_IN_COUNT,           /**< Quantidade de entradas. */
};

/** @brief UARTs de equipamento. */
enum hw_uart {
	HW_UART_CAMERA_ENTRY = 0, /**< Câmera da entrada (USART3). */
	HW_UART_CAMERA_EXIT,      /**< Câmera da saída (USART2). */
	HW_UART_PAYMENT,          /**< Terminal de pagamento (LPUART1). */
	HW_UART_COUNT,            /**< Quantidade de UARTs. */
};

/**
 * @brief Função chamada a cada borda de uma entrada, em contexto de work queue.
 *
 * @param lane Faixa da entrada.
 * @param input Entrada que mudou.
 */
typedef void (*hw_input_cb_t)(enum hw_lane lane, enum hw_input input);

/**
 * @brief Configura GPIOs, interrupções, ADC4, UARTs e SPI.
 *
 * Saídas começam desligadas e o vermelho aceso.
 *
 * @return 0 em sucesso ou errno negativo.
 */
int hw_init(void);

/**
 * @brief Registra a função chamada nas bordas das entradas.
 *
 * @param cb Função, ou NULL para não receber eventos.
 */
void hw_set_input_callback(hw_input_cb_t cb);

/**
 * @brief Escreve uma saída digital.
 *
 * @param lane Faixa.
 * @param output Saída.
 * @param on true para nível alto.
 * @return 0 em sucesso ou errno negativo.
 */
int hw_output_set(enum hw_lane lane, enum hw_output output, bool on);

/**
 * @brief Lê uma entrada digital.
 *
 * @param lane Faixa.
 * @param input Entrada.
 * @return 1 ativa, 0 inativa ou errno negativo.
 */
int hw_input_get(enum hw_lane lane, enum hw_input input);

/**
 * @brief Transmite bytes por uma UART de equipamento.
 *
 * @param uart UART de destino.
 * @param data Bytes a enviar.
 * @param len Quantidade de bytes.
 * @return 0 em sucesso ou errno negativo.
 */
int hw_uart_write(enum hw_uart uart, const uint8_t *data, size_t len);

/**
 * @brief Retira um byte recebido, sem bloquear.
 *
 * A UART é lida por polling: o modelo do Renode guarda os bytes que chegam até
 * o firmware lê-los.
 *
 * @param uart UART de origem.
 * @param byte Recebe o byte.
 * @return 0 se havia byte ou -EAGAIN se não havia.
 */
int hw_uart_read(enum hw_uart uart, uint8_t *byte);

/**
 * @brief Inicia uma conversão assíncrona do sensor de distância de uma faixa.
 *
 * Só pode haver uma conversão pendente por vez no ADC4: o chamador só inicia
 * outra depois que hw_adc_poll() informar o fim desta.
 *
 * @param lane Faixa cujo canal será convertido.
 * @return 0 em sucesso ou errno negativo.
 */
int hw_adc_start(enum hw_lane lane);

/**
 * @brief Verifica, sem bloquear, se a conversão pendente terminou.
 *
 * @param raw Recebe o código de 12 bits quando a conversão terminou.
 * @return 0 terminou, -EAGAIN ainda pendente ou errno negativo (falha).
 */
int hw_adc_poll(uint16_t *raw);

/**
 * @brief Aciona ou solta o reset do display (ativo em nível baixo no pino).
 *
 * @param asserted true para manter o display em reset.
 * @return 0 em sucesso ou errno negativo.
 */
int hw_display_reset(bool asserted);

/**
 * @brief Envia um comando ao display, seguido dos seus parâmetros.
 *
 * O byte de comando vai com D/C baixo e os parâmetros com D/C alto.
 *
 * @param cmd Código do comando ST7789V.
 * @param params Parâmetros, ou NULL.
 * @param len Quantidade de parâmetros.
 * @return 0 em sucesso ou errno negativo.
 */
int hw_display_cmd(uint8_t cmd, const uint8_t *params, size_t len);

/**
 * @brief Envia bytes de dados ao display (D/C alto), por exemplo pixels.
 *
 * @param data Bytes a enviar.
 * @param len Quantidade de bytes.
 * @return 0 em sucesso ou errno negativo.
 */
int hw_display_data(const uint8_t *data, size_t len);

#endif /* HW_H_ */
