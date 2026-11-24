/*
 * Protocolo AT da câmera de placas (REQ-02, REQ-11): só sintaxe.
 *
 *   firmware → câmera  AT+CAPTURE=<id>\r\n
 *   câmera → firmware  OK | ERROR | +PLATE:<id>,"<placa>",<confiança>
 *
 * As linhas chegam já montadas pelo line_parser (sem o CRLF).
 */
#ifndef PARKING_CAMERA_H
#define PARKING_CAMERA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Faixa canônica do ID de captura. */
#define CAMERA_ID_MIN 1U
#define CAMERA_ID_MAX 999999999U

/* "ABC1D23" + NUL. */
#define PLATE_LEN 7
#define PLATE_BUF (PLATE_LEN + 1)

enum camera_kind {
	CAMERA_INVALID = 0, /* malformada: descartar */
	CAMERA_OK,          /* pedido aceito (ainda não é a placa) */
	CAMERA_ERROR,       /* erro sem correlação: só diagnóstico */
	CAMERA_PLATE,       /* resultado de uma captura */
};

struct camera_msg {
	enum camera_kind kind;
	uint32_t id;           /* CAMERA_PLATE */
	char plate[PLATE_BUF]; /* CAMERA_PLATE */
	uint32_t confidence;   /* CAMERA_PLATE, 0..100 */
};

/**
 * Escreve "AT+CAPTURE=<id>\r\n" em out.
 * @return tamanho escrito, ou -EINVAL (id fora da faixa ou out pequeno).
 */
int camera_format_capture(char *out, size_t size, uint32_t id);

/** Interpreta uma linha da câmera. Sempre preenche m->kind. */
void camera_parse(const char *line, struct camera_msg *m);

/** true se a placa segue [A-Z]{3}[0-9][A-Z0-9][0-9]{2}. */
bool camera_plate_valid(const char *plate);

#endif /* PARKING_CAMERA_H */
