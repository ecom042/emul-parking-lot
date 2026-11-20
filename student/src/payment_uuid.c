/* payment_uuid.c — gera e valida UUIDs. Estratégia: veja payment_uuid.h. */
#include "payment_uuid.h"

static const char HEX[] = "0123456789abcdef";

/* splitmix64: embaralha um número de 64 bits (bom e minúsculo; domínio público). */
static uint64_t mix(uint64_t value)
{
	value += 0x9e3779b97f4a7c15ULL;
	value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
	value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
	return value ^ (value >> 31);
}

static bool is_dash_position(size_t i)
{
	return i == 8U || i == 13U || i == 18U || i == 23U;
}

static bool is_lower_hex(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

void payment_uuid_make(uint64_t counter, char out[PAYMENT_UUID_SIZE])
{
	/* 20 dígitos "aleatórios" = 80 bits: precisamos de DOIS embaralhamentos de 64 bits. */
	uint64_t scrambled = mix(counter);
	uint64_t scrambled2 = mix(scrambled);
	size_t nibble = 0U; /* quantos dígitos embaralhados já usamos */

	for (size_t i = 0U; i < PAYMENT_UUID_LEN; ++i) {
		if (is_dash_position(i)) {
			out[i] = '-';
		} else if (i >= 24U) {
			/* últimos 12 dígitos: o contador, do mais para o menos significativo */
			out[i] = HEX[(counter >> (4U * (35U - i))) & 0xFU];
		} else {
			uint64_t source = nibble < 16U ? scrambled : scrambled2;

			out[i] = HEX[(source >> (60U - 4U * (nibble % 16U))) & 0xFU];
			nibble++;
		}
	}
	out[14] = '4';                              /* nibble de versão: UUID v4 */
	out[19] = HEX[8U + ((scrambled2 >> 2U) & 0x3U)]; /* variante: 8, 9, a ou b */
	out[PAYMENT_UUID_LEN] = '\0';
}

bool payment_uuid_prefix_valid(const char *text)
{
	for (size_t i = 0U; i < PAYMENT_UUID_LEN; ++i) {
		/* O '\0' não é '-' nem hexadecimal: a verificação para nele, sem ler além. */
		if (is_dash_position(i) ? text[i] != '-' : !is_lower_hex(text[i])) {
			return false;
		}
	}
	return true;
}

bool payment_uuid_valid(const char *text)
{
	return payment_uuid_prefix_valid(text) && text[PAYMENT_UUID_LEN] == '\0';
}
