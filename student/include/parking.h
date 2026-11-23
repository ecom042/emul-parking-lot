#ifndef PARKING_CONTROLLER_H
#define PARKING_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "equipment_types.h"

struct gate;
struct gate_inputs;

/**
 * @brief Operações de equipamento usadas pelo controlador das faixas.
 * @details Os callbacks são executados na fila serializada e devem retornar
 * rapidamente. O adaptador controla os identificadores, quadros, prazos
 * e tentativas. O cancelamento invalida a operação antes de retornar.
 */
struct parking_equipment {
	int (*capture_start)(enum parking_lane_id lane, int64_t now);
	/**< Inicia a captura na câmera da faixa no instante informado. */
	enum equipment_result (*capture_poll)(enum parking_lane_id lane, int64_t now,
					      char plate[PARKING_PLATE_SIZE]);
	/**< Consulta a captura e entrega a placa terminada em NUL quando aceita. */
	void (*capture_cancel)(enum parking_lane_id lane);
	/**< Invalida a captura ativa da faixa. */
	int (*payment_start)(const char plate[PARKING_PLATE_SIZE], int64_t entered_at);
	/**< Solicita desenho da placa, cálculo da tarifa a partir da entrada e cobrança. */
	enum equipment_result (*payment_poll)(int64_t now);
	/**< Consulta o pagamento atual. Sucesso indica confirmação do terminal. */
	void (*payment_cancel)(void);
	/**< Invalida a cobrança e solicita o cancelamento ao terminal quando aplicável. */
};

/** @brief Registra o adaptador de equipamentos durante a inicialização.
 * @param equipment Tabela completa de callbacks com duração de armazenamento estática.
 */
void parking_set_equipment(const struct parking_equipment *equipment);

/** @brief Avança a operação da faixa após o processamento de segurança.
 * @param lane Faixa a processar.
 * @param gate Estado da cancela associado à faixa.
 * @param input Estado atual dos sensores da cancela.
 * @param beam_rose Indica uma interrupção do feixe desde o último passo.
 * @param now Uptime atual em ms.
 */
void parking_step(enum parking_lane_id lane, struct gate *gate,
		  struct gate_inputs input, bool beam_rose, int64_t now);

#endif
