/*
 * Máquina de estados de UMA faixa (entrada ou saída). As duas faixas usam
 * o mesmo código; só a saída passa pelos estados de pagamento.
 *
 *                 +-------------------- (saída) --------------------+
 *                 |                                                  v
 * STARTUP -> IDLE -> IDENTIFYING -> SHOW_PLATE -> SHOW_AMOUNT -> PAYING
 *              ^        |  (entrada)                                 |
 *              |        +------------------> OPENING <---- PAID -----+
 *              |                                |
 *              +-------- CLOSING <---- OPEN <---+  (fim de curso superior)
 *
 * Qualquer situação fora do normal -> ASSIST (motor parado, verde apagado,
 * vermelho aceso, pulso de 100 ms no buzzer). Dois tipos:
 *  - rearmável (cancela fechada: câmera falhou, lotado, sem sessão,
 *    pagamento falhou): volta a IDLE quando o carro sai (código > 1024)
 *    com o feixe livre;
 *  - travada (latched: falha de posição, fins de curso contraditórios,
 *    obstrução no fechamento, prazo de passagem, falha do ADC): só o reset.
 *
 * Entradas da máquina:
 *  - lane_tick(): a cada volta do laço; lê fins de curso e feixe, aplica
 *    prazos, desliga o buzzer;
 *  - lane_on_distance(): cada amostra NOVA do ADC daquela faixa;
 *  - lane_on_camera_line()/lane_on_payment_line(): linhas completas;
 *  - lane_on_display_done(): a thread do display terminou uma etapa;
 *  - lane_fault(): falha do ADC (compartilhado pelas duas faixas).
 */
#ifndef LANE_H_
#define LANE_H_

#include <stdbool.h>
#include <stdint.h>

#include "parking.h"

enum lane_state {
	LANE_STARTUP,     /* esperando os sensores assentarem depois do reset */
	LANE_IDLE,        /* fechada, vermelho, esperando um carro */
	LANE_IDENTIFYING, /* captura pedida, esperando +PLATE */
	LANE_SHOW_PLATE,  /* saída: display desenhando a placa */
	LANE_SHOW_AMOUNT, /* saída: display desenhando o valor */
	LANE_PAYING,      /* saída: CHARGE enviado, esperando PAID */
	LANE_OPENING,     /* motor subindo até o fim de curso superior */
	LANE_OPEN,        /* aberta, verde, esperando a passagem */
	LANE_CLOSING,     /* motor descendo até o fim de curso inferior */
	LANE_ASSIST,      /* assistência (rearmável ou travada) */
};

struct lane {
	enum lane_id id;
	enum lane_state state;
	/** Prazo do estado atual (tempo virtual, ms). */
	int64_t deadline_ms;

	/* Assistência */
	bool latched;          /* só sai com reset */
	bool buzzer_on;        /* pulso em andamento */
	int64_t buzzer_off_ms; /* quando desligar o buzzer */

	/* Câmera */
	uint32_t capture_id; /* ID da tentativa atual */
	uint8_t attempts;    /* tentativas já feitas neste carro */
	char plate[PLATE_LEN + 1];

	/* Pagamento (só saída) */
	char uuid[UUID_LEN + 1];
	bool uuid_active; /* false = UUID invalidado, nenhuma resposta vale */
	uint32_t amount;  /* centavos, congelado na primeira cobrança */
	uint8_t sends;    /* envios da cobrança atual */

	/* Passagem */
	bool beam_seen; /* feixe já ficou ativo depois de abrir */
};

/** Zera os contadores compartilhados (ID de captura, sequência de UUID). */
void lane_module_reset(void);

/** Prepara a faixa: saídas seguras e estado STARTUP. */
void lane_init(struct lane *l, enum lane_id id, int64_t now_ms);

void lane_tick(struct lane *l, int64_t now_ms);
void lane_on_distance(struct lane *l, uint16_t code, int64_t now_ms);
void lane_on_camera_line(struct lane *l, const char *line, int64_t now_ms);
void lane_on_payment_line(struct lane *l, const char *line, int64_t now_ms);
void lane_on_display_done(struct lane *l, bool ok, int64_t now_ms);
void lane_fault(struct lane *l, int64_t now_ms);

/** Nome do estado, para o console de diagnóstico. */
const char *lane_state_name(enum lane_state s);

#endif /* LANE_H_ */
