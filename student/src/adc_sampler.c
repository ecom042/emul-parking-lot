/* Escalonador do ADC4. Veja adc_sampler.h. */
#include "adc_sampler.h"

#include <errno.h>

#include "hw.h"

static struct {
	bool pending;         /* há conversão em andamento (o driver é dono) */
	enum lane_id lane;    /* canal da conversão em andamento */
	int64_t submitted_ms; /* quando ela foi disparada */
	bool failed;          /* falha travada */
} s;

void adc_sampler_reset(void)
{
	s.pending = false;
	s.lane = LANE_EXIT; /* a primeira conversão será da entrada */
	s.submitted_ms = 0;
	s.failed = false;
}

bool adc_sampler_failed(void)
{
	return s.failed;
}

bool adc_sampler_poll(int64_t now_ms, struct adc_sample *out)
{
	bool got = false;

	if (s.failed) {
		return false;
	}

	if (s.pending) {
		uint16_t raw = 0;
		int rc = hw_adc_check(&raw);

		if (rc == -EINPROGRESS) {
			/* Ainda não terminou: só o prazo pode mudar algo. */
			if (now_ms - s.submitted_ms >= ADC_DEADLINE_MS) {
				s.failed = true;
			}
			return false;
		}
		s.pending = false;
		if (rc != 0 || raw > ADC_CODE_MAX) {
			s.failed = true; /* erro do driver ou amostra impossível */
			return false;
		}
		out->lane = s.lane;
		out->code = raw;
		got = true;
	}

	/* Dispara a próxima conversão, no outro canal. */
	enum lane_id next = (s.lane == LANE_ENTRY) ? LANE_EXIT : LANE_ENTRY;

	if (hw_adc_start(next) != 0) {
		s.failed = true;
		return got;
	}
	s.lane = next;
	s.pending = true;
	s.submitted_ms = now_ms;
	return got;
}
