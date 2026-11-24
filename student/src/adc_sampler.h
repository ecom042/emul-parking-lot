/*
 * Escalonador do ADC4 (REQ-10).
 *
 * As duas faixas dividem o mesmo ADC4. O driver do Zephyr segura o ADC
 * durante uma leitura inteira, mesmo assíncrona, então UM único dono
 * (este módulo) faz uma conversão por vez, alternando os canais:
 *
 *   OCIOSO --start(canal)--> PENDENTE --check()==0--> entrega amostra
 *                              |                        e dispara o outro canal
 *                              +-- erro, amostra > 4095 ou 100 ms sem
 *                                  terminar --> FALHA (travada até o reset)
 *
 * Nunca há espera bloqueante: adc_sampler_poll() só olha a conversão e volta.
 * O buffer e o sinal da conversão são estáticos (hw.c), então uma conclusão
 * atrasada depois da falha escreve em memória válida e não rearma nada.
 */
#ifndef ADC_SAMPLER_H_
#define ADC_SAMPLER_H_

#include <stdbool.h>
#include <stdint.h>

#include "parking.h"

/** Uma amostra nova de distância, já validada. */
struct adc_sample {
	enum lane_id lane;
	uint16_t code;
};

/** Volta ao estado inicial (partida e testes). */
void adc_sampler_reset(void);

/**
 * Atende o ADC. Chame a cada volta do laço de controle.
 *
 * @param now_ms tempo virtual atual.
 * @param out    saída: preenchida quando há amostra nova.
 * @return true se *out tem uma amostra nova.
 */
bool adc_sampler_poll(int64_t now_ms, struct adc_sample *out);

/** true depois de uma falha do ADC (fica travada até o reset). */
bool adc_sampler_failed(void);

#endif /* ADC_SAMPLER_H_ */
