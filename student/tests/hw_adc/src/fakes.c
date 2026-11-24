/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>

#include "fakes.h"

DEFINE_FFF_GLOBALS;

DEFINE_FAKE_VALUE_FUNC(bool, fake_device_is_ready, const struct device *);
DEFINE_FAKE_VALUE_FUNC(int, fake_adc_channel_setup, const struct device *,
		       const struct adc_channel_cfg *);
DEFINE_FAKE_VALUE_FUNC(int, fake_adc_read_async, const struct device *,
		       const struct adc_sequence *, struct k_poll_signal *);

struct fake_conversao fake_conv;
uint32_t fake_canais[FAKE_MAX_SUBMISSOES];
const struct adc_sequence *fake_seq;
struct k_poll_signal *fake_sinal;

void fake_concluir(int resultado, uint16_t codigo)
{
	*(uint16_t *)fake_seq->buffer = codigo;
	k_poll_signal_raise(fake_sinal, resultado);
}

static int read_async_custom(const struct device *dev,
			     const struct adc_sequence *seq,
			     struct k_poll_signal *sig)
{
	unsigned int n = fake_adc_read_async_fake.call_count;

	ARG_UNUSED(dev);
	if (n >= 1U && n <= FAKE_MAX_SUBMISSOES) {
		fake_canais[n - 1U] = seq->channels;
	}
	fake_seq = seq;
	fake_sinal = sig;
	if (fake_conv.rc == 0 && fake_conv.concluir) {
		fake_concluir(fake_conv.resultado, fake_conv.codigo);
	}
	return fake_conv.rc;
}

void fakes_reset(void)
{
	RESET_FAKE(fake_device_is_ready);
	RESET_FAKE(fake_adc_channel_setup);
	RESET_FAKE(fake_adc_read_async);
	FFF_RESET_HISTORY();
	memset(&fake_conv, 0, sizeof(fake_conv));
	memset(fake_canais, 0, sizeof(fake_canais));
	fake_seq = NULL;
	fake_sinal = NULL;
	fake_device_is_ready_fake.return_val = true;
	fake_adc_read_async_fake.custom_fake = read_async_custom;
}
