/* tariff.c — implementação da tarifa. Regras e exemplos: veja tariff.h. */
#include "tariff.h"

bool tariff_compute(int64_t entered_at, int64_t now, uint32_t unit_ms, uint32_t *amount)
{
	int64_t elapsed = now - entered_at;
	int64_t limit;
	int64_t units;

	if (unit_ms == 0U) {
		return false; /* divisão por zero: configuração inválida */
	}
	if (elapsed < 0) {
		elapsed = 0;
	}
	/*
	 * Descobrimos se estourou 9999 unidades ANTES de somar/dividir, para a
	 * conta nunca estourar o int64 (mesmo com um `elapsed` absurdo).
	 */
	limit = (int64_t)TARIFF_MAX_UNITS * (int64_t)unit_ms;
	if (elapsed > limit) {
		return false;
	}
	/* ceil(elapsed / unit) em inteiros: (elapsed + unit - 1) / unit */
	units = (elapsed + (int64_t)unit_ms - 1) / (int64_t)unit_ms;
	if (units < 1) {
		units = 1; /* mínimo de 1 unidade (cobre elapsed == 0) */
	}
	*amount = TARIFF_FIRST_UNIT_CENTAVOS + (uint32_t)(units - 1) * TARIFF_NEXT_UNIT_CENTAVOS;
	return true;
}
