/*
 * Amostragem do ADC4 (REQ-10). Ver adc_sampler.h.
 */
#include "adc_sampler.h"

#include <errno.h>

#include "config.h"

static struct {
	bool pending;          /* há conversão no driver */
	enum lane_id channel;  /* faixa da conversão pendente (ou da próxima) */
	int64_t sent_ms;       /* quando a pendente foi disparada */
	bool failed;
	uint16_t code[LANE_COUNT];
	bool fresh[LANE_COUNT];
} s;

void adc_sampler_init(void)
{
	s.pending = false;
	s.channel = LANE_ENTRY;
	s.sent_ms = 0;
	s.failed = false;
	for (int i = 0; i < LANE_COUNT; i++) {
		s.code[i] = 0;
		s.fresh[i] = false;
	}
}

static void fail(void)
{
	/* Fica "pending" para sempre: o pedido nunca é reenviado. */
	s.failed = true;
}

void adc_sampler_step(int64_t now_ms)
{
	uint16_t raw = 0;
	int rc;

	if (s.failed) {
		return;
	}

	if (s.pending) {
		/* A conclusão precisa ser vista antes de envio + prazo. */
		if (now_ms - s.sent_ms >= ADC_DEADLINE_MS) {
			fail();
			return;
		}
		rc = hw_adc_poll(&raw);
		if (rc == -EINPROGRESS) {
			return; /* ainda convertendo: tenta no próximo tick */
		}
		s.pending = false;
		if (rc != 0 || raw > ADC_CODE_MAX) {
			fail();
			return;
		}
		s.code[s.channel] = raw;
		s.fresh[s.channel] = true;
		s.channel = s.channel == LANE_ENTRY ? LANE_EXIT : LANE_ENTRY;
	}

	/* Livre: dispara a conversão do próximo canal. */
	rc = hw_adc_start(s.channel);
	if (rc != 0) {
		fail();
		return;
	}
	s.pending = true;
	s.sent_ms = now_ms;
}

bool adc_sampler_take(enum lane_id lane, uint16_t *code)
{
	if (s.failed || !s.fresh[lane]) {
		return false;
	}
	s.fresh[lane] = false;
	*code = s.code[lane];
	return true;
}

bool adc_sampler_failed(void)
{
	return s.failed;
}
