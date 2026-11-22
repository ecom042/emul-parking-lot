#ifndef PARKING_EQUIPMENT_TYPES_H
#define PARKING_EQUIPMENT_TYPES_H

#define PARKING_LANES 2      /**< Número de faixas do estacionamento. */
#define PARKING_PLATE_SIZE 8 /**< Sete caracteres da placa mais o terminador NUL. */

/** Índices das faixas no controlador e nos adaptadores. */
enum parking_lane_id {
	PARKING_ENTRY, /**< Faixa de entrada. */
	PARKING_EXIT,  /**< Faixa de saída, com terminal de pagamento. */
};

/** Resultado da consulta à operação atual do equipamento. */
enum equipment_result {
	EQUIPMENT_PENDING, /**< Operação ainda em andamento. */
	EQUIPMENT_OK,      /**< Operação concluída com sucesso. */
	EQUIPMENT_FAILED,  /**< Operação encerrada com falha. */
};

#endif
