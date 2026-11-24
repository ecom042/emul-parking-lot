/*
 * Amostragem do ADC4 (REQ-10): um pedido assíncrono por vez, alternando os
 * canais das duas faixas, sem nunca bloquear o laço principal.
 *
 * Dono do pedido: enquanto uma conversão está pendente, ninguém dispara
 * outra. Se ela não terminar antes de ADC_DEADLINE_MS, ou terminar com erro
 * ou valor inválido, o amostrador entra em falha e fica assim até o reset
 * (não há como cancelar o pedido no driver; reenviar seria mexer num buffer
 * que ainda pode ser escrito).
 */
#ifndef PARKING_ADC_SAMPLER_H
#define PARKING_ADC_SAMPLER_H

#include <stdbool.h>
#include <stdint.h>

#include "hw.h"

/* Maior código válido de 12 bits. */
#define ADC_CODE_MAX 4095

/** Volta ao estado inicial (boot). */
void adc_sampler_init(void);

/** Um passo do laço: confere o pedido pendente e dispara o próximo. */
void adc_sampler_step(int64_t now_ms);

/**
 * Entrega a amostra fresca da faixa, se houver, e a consome: a mesma
 * amostra nunca é usada duas vezes para decidir.
 * @return true se havia amostra nova (e *code foi preenchido).
 */
bool adc_sampler_take(enum lane_id lane, uint16_t *code);

/** true depois de qualquer falha do ADC (travada até o reset). */
bool adc_sampler_failed(void);

#endif /* PARKING_ADC_SAMPLER_H */
