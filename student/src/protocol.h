/*
 * Formato e análise das mensagens das UARTs (REQ-02, REQ-05, REQ-11).
 * Lógica pura: recebe e devolve texto, não toca no hardware.
 *
 * Câmera (comandos AT, CR LF; o montador já tirou o terminador):
 *   firmware -> câmera   AT+CAPTURE=<id>
 *   câmera -> firmware   OK | ERROR | +PLATE:<id>,"<placa>",<confiança>
 *
 * Terminal de pagamento (P1, só LF, campos separados por UM espaço):
 *   firmware -> terminal P1 CHARGE <uuid> <placa> <valor> BRL
 *                        P1 CANCEL <uuid>
 *   terminal -> firmware P1 RESULT <uuid> PAID|CANCELLED <valor> BRL
 */
#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parking.h"

/** Maior ID de captura aceito pelo protocolo. */
#define CAPTURE_ID_MAX 999999999U
/** Maior valor de cobrança aceito pelo terminal, em centavos. */
#define AMOUNT_MAX 1000000U

/** Resultado de captura já validado. */
struct plate_result {
	uint32_t capture_id;
	char plate[PLATE_LEN + 1];
	uint32_t confidence;
};

enum payment_status {
	PAYMENT_PAID,
	PAYMENT_CANCELLED,
};

/** Resposta do terminal já validada. */
struct payment_result {
	char uuid[UUID_LEN + 1];
	enum payment_status status;
	uint32_t amount;
};

/** Placa válida: 3 letras, 1 algarismo, 1 letra ou algarismo, 2 algarismos. */
bool plate_is_valid(const char *s, size_t len);

/** UUID minúsculo no formato 8-4-4-4-12 (36 caracteres). */
bool uuid_is_valid(const char *s, size_t len);

/**
 * Decimal canônico (sem sinal, sem zero à esquerda, "0" só para zero) entre
 * min e max.
 */
bool parse_canonical_u32(const char *s, size_t len, uint32_t min, uint32_t max,
			 uint32_t *out);

/** Monta "AT+CAPTURE=<id>\r\n". @return comprimento ou -1. */
int camera_format_capture(char *buf, size_t size, uint32_t capture_id);

/** Analisa um corpo "+PLATE:...". @return 0 se válido, -1 caso contrário. */
int camera_parse_plate(const char *line, struct plate_result *out);

/** Gera o UUID da n-ésima tentativa de saída (determinístico). */
void uuid_make(char out[UUID_LEN + 1], uint32_t sequence, uint32_t salt);

/** Monta "P1 CHARGE ...\n". @return comprimento ou -1. */
int payment_format_charge(char *buf, size_t size, const char *uuid, const char *plate,
			  uint32_t amount);

/** Monta "P1 CANCEL <uuid>\n". @return comprimento ou -1. */
int payment_format_cancel(char *buf, size_t size, const char *uuid);

/** Analisa um corpo "P1 RESULT ...". @return 0 se válido, -1 caso contrário. */
int payment_parse_result(const char *line, struct payment_result *out);

#endif /* PROTOCOL_H_ */
