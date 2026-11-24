/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Falsos do FFF para o driver do ADC4 usado por hw_adc.c. O shim.h
 * redireciona as chamadas do .c para estes nomes.
 */
#ifndef TEST_HW_ADC_FAKES_H_
#define TEST_HW_ADC_FAKES_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>

DECLARE_FAKE_VALUE_FUNC(bool, fake_device_is_ready, const struct device *);
DECLARE_FAKE_VALUE_FUNC(int, fake_adc_channel_setup, const struct device *,
			const struct adc_channel_cfg *);
DECLARE_FAKE_VALUE_FUNC(int, fake_adc_read_async, const struct device *,
			const struct adc_sequence *, struct k_poll_signal *);

/** Como o falso de adc_read_async trata cada submissao. */
struct fake_conversao {
	int rc;          /**< Retorno da submissao. */
	bool concluir;   /**< Conclui ja na submissao; senao fica pendente. */
	int resultado;   /**< Resultado entregue no sinal ao concluir. */
	uint16_t codigo; /**< Codigo escrito no buffer da sequencia. */
};

extern struct fake_conversao fake_conv;

#define FAKE_MAX_SUBMISSOES 16

/** seq->channels de cada submissao, na ordem (copiado na hora da chamada). */
extern uint32_t fake_canais[FAKE_MAX_SUBMISSOES];

/** Sequencia e sinal da ultima submissao. */
extern const struct adc_sequence *fake_seq;
extern struct k_poll_signal *fake_sinal;

/** Volta os falsos ao padrao: device pronto, setup ok, conversao pendente. */
void fakes_reset(void);

/** Conclui a conversao pendente como o driver: codigo no buffer e sinal. */
void fake_concluir(int resultado, uint16_t codigo);

#endif /* TEST_HW_ADC_FAKES_H_ */
