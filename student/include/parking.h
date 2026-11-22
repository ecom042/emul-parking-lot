#ifndef PARKING_CONTROLLER_H
#define PARKING_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "equipment_types.h"

struct gate;
struct gate_inputs;

/**
 * These operations run in the serialized controller queue and must return
 * promptly. The equipment implementation owns the wire identifiers, retries,
 * framing and deadlines. Poll reports only the current operation; cancel
 * invalidates it before returning and queues any required UART transmission.
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

/* Supply the complete, static-lifetime equipment adapter during boot. */
void parking_set_equipment(const struct parking_equipment *equipment);

void parking_step(enum parking_lane_id lane, struct gate *gate,
		  struct gate_inputs input, bool beam_rose, int64_t now);

#endif
