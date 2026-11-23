#ifndef PARKING_DISTANCE_H
#define PARKING_DISTANCE_H

#include <stdbool.h>
#include <stdint.h>

#define PARKING_ADC_DEADLINE_MS 100
#define PARKING_ARRIVAL_CODE 683
#define PARKING_DEPARTURE_CODE 1024

/** @brief Última conversão assíncrona concluída para uma faixa. */
struct distance_sample {
	int64_t observed_at; /**< Uptime em ms quando o controlador observou a conclusão. */
	uint16_t code;       /**< Código bruto de 12 bits do ADC4 (0 a 4095). */
	bool valid;          /**< A faixa já possui uma amostra concluída com sucesso. */
};

/** @brief Configura o ADC4 antes do início da fila do controlador.
 * @return Zero em caso de sucesso ou um código de erro negativo.
 */
int distance_init(void);
/** @brief Avança a aquisição assíncrona sem esperar pela conversão.
 * @details Após a inicialização, a fila do controlador é dona da aquisição.
 * @param now Uptime atual em ms.
 */
void distance_step(int64_t now);
/** @brief Consulta a falha persistente da aquisição.
 * @return Verdadeiro se ocorreu uma falha que exige reinicialização.
 */
bool distance_faulted(void);
/** @brief Obtém uma amostra recente da faixa.
 * @param lane Índice válido da faixa.
 * @param now Uptime atual em ms.
 * @param[out] code Destino do código bruto, preenchido em caso de sucesso.
 * @return Verdadeiro se há uma amostra válida e recente.
 */
bool distance_read(unsigned int lane, int64_t now, uint16_t *code);

#endif
