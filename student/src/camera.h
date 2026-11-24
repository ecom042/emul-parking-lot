/**
 * @file camera.h
 * @brief Protocolo AT das câmeras: pedido de captura e leitura do resultado.
 */

#ifndef CAMERA_H_
#define CAMERA_H_

#include <stdint.h>

#include "hw.h"

/** @brief Caracteres de uma placa, sem o NUL. */
#define CAMERA_PLATE_LEN 7

/** @brief Confiança mínima para aceitar uma placa. */
#define CAMERA_MIN_CONFIDENCE 85

/** @brief Maior ID de captura aceito pelo protocolo. */
#define CAMERA_MAX_ID 999999999U

/**
 * @brief Envia AT+CAPTURE=<id> pela UART da câmera.
 *
 * @param uart UART da câmera.
 * @param capture_id ID da captura, 1..CAMERA_MAX_ID.
 * @return 0 em sucesso, -EINVAL para ID fora da faixa ou o erro da UART.
 */
int camera_request(enum hw_uart uart, uint32_t capture_id);

/**
 * @brief Interpreta um quadro +PLATE:<id>,"<placa>",<confiança>.
 *
 * Números precisam ser decimais canônicos (sem zero à esquerda) e a placa
 * seguir [A-Z]{3}[0-9][A-Z0-9][0-9]{2}.
 *
 * @param frame Quadro sem o terminador.
 * @param capture_id ID da captura em andamento.
 * @param plate Recebe a placa, com NUL; precisa de CAMERA_PLATE_LEN + 1 bytes.
 * @param confidence Recebe a confiança, 0..100.
 * @return 0 se o quadro é um resultado válido dessa captura ou -EINVAL.
 */
int camera_parse_plate(const char *frame, uint32_t capture_id, char *plate,
		       unsigned int *confidence);

#endif /* CAMERA_H_ */
