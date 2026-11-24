/*
 * Constantes dos requisitos, todas num só lugar.
 *
 * Uma mudança pedida na sabatina (prazo, limiar, número de tentativas)
 * deve ser feita aqui. O REQ de origem está ao lado de cada valor.
 */
#ifndef PARKING_CONFIG_H
#define PARKING_CONFIG_H

/* Período do laço principal (ms). Os modelos andam em passos de 10 ms. */
#define TICK_MS 10

/*
 * REQ-01: espera antes de conferir os fins de curso na partida (ms). Logo
 * após o boot as entradas ainda não refletem os modelos; o motor fica
 * desligado e o vermelho aceso durante a espera.
 */
#define STARTUP_SETTLE_MS 20

/* REQ-06: prazo para abrir ou fechar a cancela (ms). */
#define MOTOR_DEADLINE_MS 2500

/* REQ-09: duração do pulso do buzzer na assistência (ms). */
#define BUZZER_PULSE_MS 100

/* REQ-02/03: código do ADC abaixo disto = carro presente (~1000 mm). */
#define DIST_PRESENT_BELOW 683

/* REQ-03/07/09: código do ADC acima disto = carro ausente (~1500 mm). */
#define DIST_ABSENT_ABOVE 1024

/* REQ-02: câmera. */
#define CAMERA_TIMEOUT_MS 1000
#define CAMERA_MAX_ATTEMPTS 3
#define CAMERA_MIN_CONFIDENCE 85

/* REQ-05: terminal de pagamento. */
#define PAYMENT_TIMEOUT_MS 1000
#define PAYMENT_MAX_SENDS 3

/* REQ-07: prazo para a passagem completa depois de abrir (ms). */
#define PASSAGE_TIMEOUT_MS 10000

/* REQ-10: prazo de uma conversão do ADC (ms). */
#define ADC_DEADLINE_MS 100

/* REQ-11: quadro máximo (com terminador) e prazo de montagem de uma linha. */
#define UART_FRAME_MAX 128
#define UART_ASSEMBLY_MS 1000

/* REQ-12: acima disto a tarifa pede assistência em vez de cobrar. */
#define TARIFF_MAX_UNITS 9999

#endif /* PARKING_CONFIG_H */
