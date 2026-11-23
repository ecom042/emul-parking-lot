/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file presence.c
 * @brief Presenca do carro: histerese ADC_ARRIVE/ADC_LEAVE e debounce de
 *        ADC_DEBOUNCE_N amostras consecutivas.
 */

#include "parking_limits.h"
#include "presence.h"

enum presence_change presence_update(struct presence *p, uint16_t code)
{
	/* A amostra empurra para o lado oposto ao estado aceito? */
	bool opposite = p->present ? (code > ADC_LEAVE) : (code < ADC_ARRIVE);

	if (!opposite) {
		/* Qualquer amostra fora da faixa zera a contagem. */
		p->count = 0;
		return PRESENCE_NONE;
	}

	p->count++;
	if (p->count < ADC_DEBOUNCE_N) {
		return PRESENCE_NONE;
	}

	p->count = 0;
	p->present = !p->present;
	return p->present ? PRESENCE_ARRIVED : PRESENCE_LEFT;
}
