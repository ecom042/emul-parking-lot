#ifndef PARKING_GATE_H
#define PARKING_GATE_H

#include <stdbool.h>
#include <stdint.h>

#define GATE_MOTION_MS 2500
#define GATE_BUZZER_MS 100

/** Estados da máquina de segurança de uma cancela. */
enum gate_state {
	GATE_CLOSED,     /**< Fechada ou aguardando a primeira leitura no boot. */
	GATE_OPENING,    /**< Motor abrindo, à espera do fim de curso superior. */
	GATE_OPEN,       /**< Fim de curso superior confirmado e motor parado. */
	GATE_CLOSING,    /**< Motor fechando, sob supervisão do feixe. */
	GATE_ASSISTANCE, /**< Movimento interrompido e sinal vermelho aceso. */
};

/** Leitura dos sensores usada em um passo do controle. */
struct gate_inputs {
	bool upper; /**< Fim de curso superior ativo. */
	bool lower; /**< Fim de curso inferior ativo. */
	bool beam;  /**< Feixe interrompido ou obstrução capturada no fechamento. */
};

/** Saídas encaminhadas pelo adaptador de GPIO. */
enum gate_output {
	GATE_ENABLE,    /**< Ativa o motor. */
	GATE_DIRECTION, /**< Ativo para abrir, inativo para fechar. */
	GATE_RED,       /**< Sinal vermelho da faixa. */
	GATE_GREEN,     /**< Sinal verde da faixa. */
	GATE_BUZZER,    /**< Aviso sonoro de assistência. */
};

/** Estado e dependências de uma instância da máquina de segurança. */
struct gate {
	enum gate_state state; /**< Estado atual da cancela. */
	int64_t deadline;      /**< Prazo de movimento ou fim do pulso sonoro, em ms de uptime. */
	bool latched;          /**< Falha persistente que exige reinicialização. */
	bool buzzing;          /**< Pulso sonoro ativo, aguardando desligamento. */
	int (*write)(void *context, enum gate_output output, bool active);
	/**< Escreve uma saída lógica e retorna zero em caso de sucesso. */
	void *context;         /**< Contexto repassado ao adaptador de saída. */
};

/* Call the state-machine functions from one serialized control context. */
void gate_assist(struct gate *gate, int64_t now, bool latch);
void gate_step(struct gate *gate, struct gate_inputs input, int64_t now);
bool gate_open(struct gate *gate, struct gate_inputs input, int64_t now);
bool gate_close(struct gate *gate, struct gate_inputs input, int64_t now);
bool gate_rearm(struct gate *gate, struct gate_inputs input, bool vehicle_gone);

#endif
