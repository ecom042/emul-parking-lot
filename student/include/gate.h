#ifndef PARKING_GATE_H
#define PARKING_GATE_H

#include <stdbool.h>
#include <stdint.h>

#define GATE_MOTION_MS 2500
#define GATE_BUZZER_MS 100

/** @enum gate_state
 * @brief Estados da máquina de segurança de uma cancela.
 */
enum gate_state {
	GATE_CLOSED,     /**< Fechada ou aguardando a primeira leitura no boot. */
	GATE_OPENING,    /**< Motor abrindo, à espera do fim de curso superior. */
	GATE_OPEN,       /**< Fim de curso superior confirmado e motor parado. */
	GATE_CLOSING,    /**< Motor fechando, sob supervisão do feixe. */
	GATE_ASSISTANCE, /**< Movimento interrompido e sinal vermelho aceso. */
};

/** @brief Leitura dos sensores usada em um passo do controle. */
struct gate_inputs {
	bool upper; /**< Fim de curso superior ativo. */
	bool lower; /**< Fim de curso inferior ativo. */
	bool beam;  /**< Feixe interrompido ou obstrução capturada no fechamento. */
};

/** @enum gate_output
 * @brief Saídas encaminhadas pelo adaptador de GPIO.
 */
enum gate_output {
	GATE_ENABLE,    /**< Ativa o motor. */
	GATE_DIRECTION, /**< Ativo para abrir, inativo para fechar. */
	GATE_RED,       /**< Sinal vermelho da faixa. */
	GATE_GREEN,     /**< Sinal verde da faixa. */
	GATE_BUZZER,    /**< Aviso sonoro de assistência. */
};

/** @brief Estado e dependências de uma instância da máquina de segurança.
 * @details As operações devem ser chamadas de um único contexto de controle serializado.
 */
struct gate {
	enum gate_state state; /**< Estado atual da cancela. */
	int64_t deadline;      /**< Prazo de movimento ou fim do pulso sonoro, em ms de uptime. */
	bool latched;          /**< Falha persistente que exige reinicialização. */
	bool buzzing;          /**< Pulso sonoro ativo, aguardando desligamento. */
	int (*write)(void *context, enum gate_output output, bool active);
	/**< Escreve uma saída lógica e retorna zero em caso de sucesso. */
	void *context;         /**< Contexto repassado ao adaptador de saída. */
};

/** @brief Interrompe o movimento e sinaliza assistência.
 * @param gate Cancela afetada.
 * @param now Uptime atual em ms.
 * @param latch Se verdadeiro, exige reinicialização para liberar a cancela.
 */
void gate_assist(struct gate *gate, int64_t now, bool latch);
/** @brief Processa sensores e prazos da cancela.
 * @param gate Cancela a atualizar.
 * @param input Estado atual dos sensores.
 * @param now Uptime atual em ms.
 */
void gate_step(struct gate *gate, struct gate_inputs input, int64_t now);
/** @brief Solicita a abertura de uma cancela fechada.
 * @param gate Cancela a abrir.
 * @param input Estado atual dos sensores.
 * @param now Uptime atual em ms.
 * @return Verdadeiro se o movimento de abertura foi iniciado.
 */
bool gate_open(struct gate *gate, struct gate_inputs input, int64_t now);
/** @brief Solicita o fechamento de uma cancela aberta com o feixe livre.
 * @param gate Cancela a fechar.
 * @param input Estado atual dos sensores.
 * @param now Uptime atual em ms.
 * @return Verdadeiro se o movimento de fechamento foi iniciado.
 */
bool gate_close(struct gate *gate, struct gate_inputs input, int64_t now);
/** @brief Tenta liberar uma cancela em assistência recuperável.
 * @param gate Cancela a rearmar.
 * @param input Estado atual dos sensores.
 * @param vehicle_gone Indica que o veículo já se afastou da faixa.
 * @return Verdadeiro se a cancela voltou ao estado fechado.
 */
bool gate_rearm(struct gate *gate, struct gate_inputs input, bool vehicle_gone);

#endif
