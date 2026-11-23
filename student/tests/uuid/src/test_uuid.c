/* SPDX-License-Identifier: Apache-2.0 */
/* UUID: formato 8-4-4-4-12 e unicidade mesmo com entropia fraca. */
#include <string.h>

#include <zephyr/ztest.h>

#include "uuid.h"

static char vistos[64][UUID_LEN + 1];

ZTEST_SUITE(uuid, NULL, NULL, NULL, NULL, NULL);

ZTEST(uuid, test_formato)
{
	char out[UUID_LEN + 1];

	uuid_generate(out);

	zassert_equal(strlen(out), UUID_LEN, "36 caracteres");
	zassert_equal(out[8], '-');
	zassert_equal(out[13], '-');
	zassert_equal(out[18], '-');
	zassert_equal(out[23], '-');

	for (int i = 0; i < UUID_LEN; i++) {
		char c = out[i];
		bool dash = (i == 8 || i == 13 || i == 18 || i == 23);
		bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');

		zassert_true(dash ? c == '-' : hex, "caractere %d invalido", i);
	}
}

ZTEST(uuid, test_unicidade)
{
	uuid_generate(vistos[0]);

	for (int i = 1; i < (int)ARRAY_SIZE(vistos); i++) {
		uuid_generate(vistos[i]);
		for (int j = 0; j < i; j++) {
			zassert_true(strcmp(vistos[i], vistos[j]) != 0,
				     "UUID repetido em %d/%d", i, j);
		}
	}
}
