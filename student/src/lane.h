/*
 * Máquina de estados de uma faixa. As duas faixas usam o mesmo código;
 * cada uma tem a sua struct lane.
 *
 *   BOOT → IDLE → IDENTIFY → [saída: PAYING] → OPENING → PASSAGE → CLOSING → IDLE
 *   qualquer anomalia → ASSIST (rearmável) ou LOCKED (só o reset tira)
 */
#ifndef PARKING_LANE_H
#define PARKING_LANE_H

#include <stdbool.h>
#include <stdint.h>

#include "barrier.h"
#include "camera.h"
#include "hw.h"
#include "line_parser.h"
#include "payment.h"

enum lane_state {
	LANE_BOOT = 0, /* partida: motor desligado, esperando as entradas */
	LANE_IDLE,     /* cancela fechada, esperando carro */
	LANE_IDENTIFY, /* pedindo a placa à câmera */
	LANE_PAYING,   /* saída: cobrança enviada, esperando PAID */
	LANE_OPENING,  /* abrindo até o fim de curso superior */
	LANE_PASSAGE,  /* aberta, verde aceso, esperando o carro passar */
	LANE_CLOSING,  /* fechando até o fim de curso inferior */
	LANE_ASSIST,   /* assistência rearmável (cancela fechada) */
	LANE_LOCKED,   /* assistência travada até o reset */
};

struct lane {
	enum lane_id id;
	enum lane_state state;
	struct barrier barrier;

	/* Buzzer (REQ-09). */
	bool buzzer_on;
	int64_t buzzer_off_ms;

	/* Distância (REQ-03). */
	uint16_t dist;    /* último código fresco do ADC */
	bool dist_fresh;  /* chegou amostra nova neste passo */
	bool armed;       /* pode disparar captura (o carro anterior saiu) */

	/* Câmera (REQ-02). */
	struct line_parser cam_parser;
	uint32_t next_capture_id; /* contador da faixa; nunca repete no boot */
	uint32_t capture_id;      /* ID esperado; 0 = nenhum pedido válido */
	int attempt;              /* 1..CAMERA_MAX_ATTEMPTS */
	int64_t deadline_ms;      /* prazo da tentativa ou da passagem */
	char plate[PLATE_BUF];    /* placa aceita no ciclo atual */

	/* Passagem (REQ-07). */
	bool beam_seen;
	bool passage_late; /* assistência interrompeu a passagem; ainda conta */

	/* Pagamento, só na saída (REQ-05). */
	struct line_parser pay_parser;
	char uuid[UUID_BUF];         /* UUID ativo; vazio = nenhuma cobrança vale */
	uint32_t amount;             /* valor congelado na primeira cobrança */
	int sends;                   /* envios da cobrança atual (1..3) */
	char charge_line[UART_FRAME_MAX + 1]; /* a mesma linha em cada reenvio */
	size_t charge_len;
};

/** Zera a faixa. Não acessa o hardware. */
void lane_init(struct lane *l, enum lane_id id);

/**
 * Partida segura (REQ-01): acende o vermelho. Os fins de curso são
 * conferidos no primeiro passo depois de STARTUP_SETTLE_MS.
 */
void lane_start(struct lane *l, int64_t now_ms);

/** Um passo do laço principal (a cada TICK_MS). */
void lane_step(struct lane *l, int64_t now_ms);

/** Chamado pela work quando uma entrada da faixa muda (parada urgente). */
void lane_on_input(struct lane *l);

#endif /* PARKING_LANE_H */
