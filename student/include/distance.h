#ifndef PARKING_DISTANCE_H
#define PARKING_DISTANCE_H

#include <stdbool.h>
#include <stdint.h>

#define PARKING_LANES 2
#define PARKING_ADC_DEADLINE_MS 100
#define PARKING_ARRIVAL_CODE 683
#define PARKING_DEPARTURE_CODE 1024

/** Última conversão assíncrona concluída para uma faixa. */
struct distance_sample {
	int64_t observed_at; /**< Uptime em ms quando o controlador observou a conclusão. */
	uint16_t code;       /**< Código bruto de 12 bits do ADC4 (0 a 4095). */
	bool valid;          /**< A faixa já possui uma amostra concluída com sucesso. */
};

/* Owned by the controller work queue after setup. */
int distance_init(void);
void distance_step(int64_t now);
bool distance_faulted(void);
bool distance_read(unsigned int lane, int64_t now, uint16_t *code);

#endif
