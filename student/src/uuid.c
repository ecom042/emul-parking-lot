/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file uuid.c
 * @brief UUID hexadecimal minusculo 8-4-4-4-12.
 */

#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "uuid.h"

static const char hex[] = "0123456789abcdef";

void uuid_generate(char out[UUID_LEN + 1])
{
	static uint32_t counter;
	uint8_t raw[16];
	uint64_t tempo;
	uint64_t serie;
	int i;

	/*
	 * O contrato pede um identificador novo por tentativa, nao seguranca:
	 * "correlation is not authentication" (device-protocols.md). A placa
	 * nao expoe fonte de entropia no devicetree, entao o UUID vem de tempo
	 * virtual + contador -- que continuam distintos mesmo se o tempo parar.
	 */
	tempo = (uint64_t)k_uptime_get();
	serie = ((uint64_t)(++counter) << 32) | (uint32_t)(tempo >> 8);
	for (i = 0; i < 8; i++) {
		raw[i] = (uint8_t)(tempo >> (8 * i));
		raw[8 + i] = (uint8_t)(serie >> (8 * i));
	}

	/* Versao 4 e variante RFC, como qualquer UUID textual. */
	raw[6] = (uint8_t)((raw[6] & 0x0fU) | 0x40U);
	raw[8] = (uint8_t)((raw[8] & 0x3fU) | 0x80U);

	for (i = 0; i < 16; i++) {
		int pos = i * 2 + (i >= 4) + (i >= 6) + (i >= 8) + (i >= 10);

		out[pos] = hex[raw[i] >> 4];
		out[pos + 1] = hex[raw[i] & 0x0fU];
	}
	out[8] = '-';
	out[13] = '-';
	out[18] = '-';
	out[23] = '-';
	out[UUID_LEN] = '\0';
}
