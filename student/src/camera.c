/*
 * camera.c — máquina de captura da câmera (lógica, sem tocar no hardware).
 * Visão geral e contrato: veja camera.h.
 *
 * DIAGRAMA DE UMA CAPTURA (por faixa):
 *
 *   start ──► [WAITING, tentativa 1, prazo = agora + 1000 ms]
 *                │
 *                │  a cada poll(): lê bytes da UART -> parser -> quadros
 *                │
 *                ├─ +PLATE do id atual, confiança >= 85, dentro do prazo ──► OK
 *                ├─ +PLATE do id atual, confiança <  85 ─┐
 *                ├─ prazo vencido (sem resposta completa) ┤
 *                │                                         ▼
 *                │                       tentativa < 3 ? nova tentativa (id novo)
 *                │                                  : FAILED (assistência)
 *                └─ erro de UART / de envio ──► FAILED
 *
 *   cancel ──► volta a IDLE na hora (id invalidado).
 *
 * DUAS IDEIAS-CHAVE:
 *   1. "id novo a cada tentativa": a câmera responde com o id do pedido. Se o
 *      id da resposta não for o ATUAL, a resposta é velha (de uma tentativa
 *      vencida ou abandonada) e é IGNORADA. É assim que respostas atrasadas
 *      nunca autorizam nada (REQ-02, REQ-13).
 *   2. Os bytes são sempre lidos do UART, mesmo os que serão ignorados, para o
 *      analisador continuar sincronizado com as fronteiras de linha (LF).
 */
#include <errno.h>
#include <string.h>

#include "camera.h"
#include "camera_hw.h"
#include "camera_parser.h"

/* O buffer de placa do controle (8) = 7 caracteres + '\0'. Se alguém mudar um, o build avisa. */
_Static_assert(CAMERA_PLATE_LEN + 1U == PARKING_PLATE_SIZE, "placa: 7 caracteres + NUL");

#define COMMAND_PREFIX "AT+CAPTURE="
/* "AT+CAPTURE=" (11) + até 9 dígitos + CR LF (2) = 22; sobra folga. */
#define COMMAND_MAX 24U

enum camera_phase {
	CAMERA_IDLE,    /* nenhuma captura em andamento */
	CAMERA_WAITING, /* pedido enviado, esperando o resultado */
};

/* Veredito sobre os quadros lidos em UMA chamada de poll. */
enum camera_verdict {
	VERDICT_NONE,   /* nada que decida a tentativa */
	VERDICT_ACCEPT, /* placa boa para o id atual */
	VERDICT_REJECT, /* resposta do id atual, mas confiança baixa */
};

/* Estado de uma câmera (uma por faixa). */
struct camera_lane {
	enum camera_phase phase;
	uint8_t attempts;           /* quantas tentativas já foram ENVIADAS nesta captura */
	uint32_t next_id;           /* próximo id novo a usar (nunca se repete na execução) */
	uint32_t current_id;        /* id da tentativa em andamento */
	int64_t deadline;           /* instante em que a tentativa atual vence */
	struct camera_parser parser; /* montador de linhas desta UART */
};

static struct camera_lane cams[PARKING_LANES];

static bool valid_lane(enum parking_lane_id lane)
{
	return (unsigned int)lane < PARKING_LANES;
}

/* Escreve "AT+CAPTURE=<id>\r\n" em out e devolve o tamanho. (Sem snprintf: simples e previsível.) */
static size_t build_command(char out[COMMAND_MAX], uint32_t id)
{
	char digits[10];
	size_t count = 0U;
	size_t length = sizeof(COMMAND_PREFIX) - 1U;

	memcpy(out, COMMAND_PREFIX, length);
	do { /* extrai os dígitos de trás para frente */
		digits[count++] = (char)('0' + id % 10U);
		id /= 10U;
	} while (id > 0U);
	while (count > 0U) { /* e escreve na ordem certa */
		out[length++] = digits[--count];
	}
	out[length++] = '\r';
	out[length++] = '\n';
	return length;
}

/*
 * Começa uma tentativa: escolhe um id novo, envia o pedido e arma o prazo.
 * Falha (e NÃO deixa a captura em WAITING) se os ids acabaram ou o envio falhou.
 */
static int begin_attempt(unsigned int index, struct camera_lane *cam, int64_t now)
{
	char command[COMMAND_MAX];
	size_t length;
	int rc;

	if (cam->next_id > CAMERA_ID_MAX) {
		return -ENOSPC;
	}
	cam->current_id = cam->next_id++;
	length = build_command(command, cam->current_id);
	rc = camera_hw_send(index, (const uint8_t *)command, length);
	if (rc < 0) {
		return rc;
	}
	cam->attempts++;
	cam->deadline = now + CAMERA_ATTEMPT_TIMEOUT_MS;
	cam->phase = CAMERA_WAITING;
	return 0;
}

/* Gasta uma tentativa: tenta de novo se ainda há, senão falha. */
static enum equipment_result retry(unsigned int index, struct camera_lane *cam, int64_t now)
{
	if (cam->attempts >= CAMERA_ATTEMPTS || begin_attempt(index, cam, now) < 0) {
		return EQUIPMENT_FAILED;
	}
	return EQUIPMENT_PENDING;
}

/*
 * Este quadro decide a tentativa atual? Só um +PLATE que:
 *   - é do id ATUAL (senão é resposta velha),
 *   - chegou ANTES do prazo (senão a tentativa já venceu),
 * decide. Se a confiança for >= 85, aceita; senão rejeita (gasta a tentativa).
 * OK, ERROR e INVALID nunca decidem — e nunca mexem no prazo.
 */
static enum camera_verdict judge(const struct camera_lane *cam, const struct camera_frame *frame,
				 int64_t now)
{
	if (frame->kind != CAMERA_FRAME_PLATE || frame->id != cam->current_id ||
	    now >= cam->deadline) {
		return VERDICT_NONE;
	}
	return frame->confidence >= CAMERA_MIN_CONFIDENCE ? VERDICT_ACCEPT : VERDICT_REJECT;
}

/*
 * Esvazia a UART: lê os bytes que já chegaram, passa pelo parser e julga os
 * quadros. É LIMITADO (no máximo CAMERA_READ_ROUNDS blocos) para que um fluxo
 * sem fim nunca prenda o trabalho de segurança do firmware.
 * Devolve < 0 se a leitura falhou. O primeiro quadro decisivo vence; os
 * seguintes (por exemplo, um +PLATE duplicado) são ignorados.
 */
static int pump(unsigned int index, struct camera_lane *cam, int64_t now,
		enum camera_verdict *verdict, struct camera_frame *match)
{
	uint8_t chunk[CAMERA_READ_CHUNK];

	*verdict = VERDICT_NONE;
	for (int round = 0; round < CAMERA_READ_ROUNDS; ++round) {
		int count = camera_hw_read(index, chunk, sizeof(chunk));

		if (count < 0) {
			return count;
		}
		for (int i = 0; i < count; ++i) {
			struct camera_frame frame;

			camera_parser_feed(&cam->parser, chunk[i], now, &frame);
			if (*verdict == VERDICT_NONE) {
				*verdict = judge(cam, &frame, now);
				*match = frame;
			}
		}
		if ((size_t)count < sizeof(chunk)) {
			break; /* o bloco veio incompleto: não há mais nada a ler agora */
		}
	}
	return 0;
}

/* Um passo da captura em andamento. */
static enum equipment_result service(unsigned int index, struct camera_lane *cam, int64_t now,
				     char plate[PARKING_PLATE_SIZE])
{
	enum camera_verdict verdict;
	struct camera_frame match = { 0 };

	if (pump(index, cam, now, &verdict, &match) < 0) {
		return EQUIPMENT_FAILED;
	}
	if (verdict == VERDICT_ACCEPT) {
		memcpy(plate, match.plate, PARKING_PLATE_SIZE);
		return EQUIPMENT_OK;
	}
	if (verdict == VERDICT_REJECT || now >= cam->deadline) {
		return retry(index, cam, now);
	}
	return EQUIPMENT_PENDING;
}

void camera_reset(void)
{
	for (size_t i = 0U; i < PARKING_LANES; ++i) {
		memset(&cams[i], 0, sizeof(cams[i]));
		cams[i].next_id = 1U; /* ids canônicos começam em 1 */
		camera_parser_init(&cams[i].parser);
	}
}

int camera_init(void)
{
	camera_reset();
	return camera_hw_init();
}

int camera_capture_start(enum parking_lane_id lane, int64_t now)
{
	struct camera_lane *cam;
	enum camera_verdict ignored;
	struct camera_frame unused = { 0 };
	int rc;

	if (!valid_lane(lane)) {
		return -EINVAL;
	}
	cam = &cams[lane];
	cam->phase = CAMERA_IDLE;
	cam->attempts = 0U;

	/*
	 * Antes de pedir uma captura nova, joga fora o que sobrou na UART (por
	 * exemplo, a resposta atrasada de uma captura abandonada). O veredito é
	 * descartado de propósito: isto é só limpeza.
	 */
	rc = pump((unsigned int)lane, cam, now, &ignored, &unused);
	if (rc < 0) {
		return rc;
	}
	return begin_attempt((unsigned int)lane, cam, now);
}

enum equipment_result camera_capture_poll(enum parking_lane_id lane, int64_t now,
					  char plate[PARKING_PLATE_SIZE])
{
	struct camera_lane *cam;
	enum equipment_result result;

	if (!valid_lane(lane)) {
		return EQUIPMENT_FAILED;
	}
	cam = &cams[lane];
	if (cam->phase != CAMERA_WAITING) {
		return EQUIPMENT_FAILED; /* sem captura em andamento: nunca autoriza */
	}
	result = service((unsigned int)lane, cam, now, plate);
	if (result != EQUIPMENT_PENDING) {
		cam->phase = CAMERA_IDLE; /* OK e FAILED são resultados de uma vez só */
	}
	return result;
}

void camera_capture_cancel(enum parking_lane_id lane)
{
	if (valid_lane(lane)) {
		cams[lane].phase = CAMERA_IDLE; /* qualquer resposta tardia agora é ignorada */
	}
}

void camera_set_next_id(enum parking_lane_id lane, uint32_t id)
{
	if (valid_lane(lane)) {
		cams[lane].next_id = id;
	}
}
