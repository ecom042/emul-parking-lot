/*
 * Constantes do controlador de estacionamento.
 *
 * Todos os limiares e prazos do desafio ficam aqui, num só lugar. Na sabatina
 * a "mudança ao vivo" costuma ser trocar um destes valores (um prazo, um
 * limiar ou uma regra de rearme): mude aqui e rode o cenário correspondente.
 *
 * Referências: docs/desafio.md (REQ-xx) e docs/device-protocols.md.
 */
#ifndef PARKING_H_
#define PARKING_H_

#include <stdint.h>

/* ---- Distância (ADC4, código bruto de 12 bits) -------------------------- */

/** REQ-02: código < 683 (cerca de 1000 mm) com a cancela fechada = chegada. */
#define DIST_ARRIVE_BELOW 683
/** REQ-03/07/09: código > 1024 (cerca de 1500 mm) = carro saiu / passou. */
#define DIST_LEAVE_ABOVE 1024
/** Maior código válido de 12 bits. Acima disso a amostra é inválida. */
#define ADC_CODE_MAX 4095
/** REQ-10: a conversão precisa terminar antes de 100 ms após o envio. */
#define ADC_DEADLINE_MS 100

/* ---- Cancela (REQ-06, REQ-08) ------------------------------------------- */

/** Orçamento para abrir ou fechar, do motor ligado ao fim de curso. */
#define MOTOR_BUDGET_MS 2500
/** REQ-07: depois de aberta, o carro tem 10 s para passar pelo feixe. */
#define PASSAGE_BUDGET_MS 10000
/** REQ-09: duração do pulso do buzzer na assistência. */
#define BUZZER_PULSE_MS 100
/**
 * REQ-01: depois do reset os modelos só redirigem as entradas na primeira
 * amostra de 10 ms (docs/physical-model.md, "Sensor settling after reset").
 * Esperamos um pouco mais que isso antes de avaliar os fins de curso.
 */
#define STARTUP_SETTLE_MS 50

/* ---- Câmera (REQ-02) ---------------------------------------------------- */

/** Confiança mínima aceita (85 é aceito). */
#define CAMERA_MIN_CONFIDENCE 85
/** Número máximo de tentativas de captura, cada uma com ID novo. */
#define CAMERA_MAX_ATTEMPTS 3
/** Prazo de cada tentativa, incluindo a resposta fragmentada completa. */
#define CAMERA_ATTEMPT_MS 1000

/* ---- Pagamento (REQ-05, REQ-12) ----------------------------------------- */

/** Número máximo de envios da mesma cobrança (mesmo UUID e valor). */
#define PAYMENT_MAX_SENDS 3
/** Prazo de cada envio. */
#define PAYMENT_SEND_MS 1000
/** Tempo máximo para a thread do display desenhar uma etapa da tela. */
#define DISPLAY_BUDGET_MS 3000

/* ---- UART (REQ-11) ------------------------------------------------------ */

/** Quadro máximo, incluindo o terminador (CR LF ou LF). */
#define UART_FRAME_MAX 128
/** Uma linha começada precisa terminar em até 1000 ms. */
#define UART_ASSEMBLY_MS 1000

/* ---- Tamanhos ----------------------------------------------------------- */

/** Placa: 7 caracteres + '\0'. */
#define PLATE_LEN 7
/** UUID 8-4-4-4-12: 36 caracteres + '\0'. */
#define UUID_LEN 36

/** As duas faixas. O índice é usado em tabelas (sensores, saídas, ADC). */
enum lane_id {
	LANE_ENTRY = 0,
	LANE_EXIT = 1,
	LANE_COUNT = 2,
};

#endif /* PARKING_H_ */
