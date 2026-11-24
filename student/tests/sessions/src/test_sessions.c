/* SPDX-License-Identifier: Apache-2.0 */
/* Sessoes: vaga livre, placa duplicada, tabela cheia e reset. */
#include <errno.h>

#include <zephyr/ztest.h>

#include "sessions.h"

static void limpar(void *fixture)
{
	ARG_UNUSED(fixture);
	sessions_reset();
}

ZTEST_SUITE(sessions, NULL, NULL, limpar, NULL, NULL);

ZTEST(sessions, test_vazio)
{
	zassert_equal(sessions_count(), 0);
	zassert_false(sessions_full());
	zassert_false(sessions_has("ABC1D23"));
}

ZTEST(sessions, test_cria_e_le_instante)
{
	int64_t entry = 0;

	zassert_ok(sessions_add("ABC1D23", 500));
	zassert_true(sessions_has("ABC1D23"));
	zassert_equal(sessions_count(), 1);
	zassert_ok(sessions_entry_time("ABC1D23", &entry));
	zassert_equal(entry, 500);
}

ZTEST(sessions, test_duplicada_e_invalida)
{
	zassert_ok(sessions_add("ABC1D23", 500));
	zassert_equal(sessions_add("ABC1D23", 900), -EEXIST);
	zassert_equal(sessions_add(NULL, 0), -EINVAL);
	zassert_equal(sessions_add("ABC", 0), -EINVAL);
	zassert_false(sessions_has(NULL));
	zassert_equal(sessions_entry_time("ABC1D23", NULL), -EINVAL);
	zassert_equal(sessions_entry_time("ZZZ9Z99", &(int64_t){0}), -ENOENT);
}

ZTEST(sessions, test_lotada)
{
	zassert_ok(sessions_add("ABC1234", 1));
	zassert_ok(sessions_add("XYZ9A88", 2));
	zassert_ok(sessions_add("QWE4B21", 3));

	zassert_false(sessions_full());
	zassert_ok(sessions_add("RTY7C33", 4));
	zassert_true(sessions_full());
	zassert_equal(sessions_count(), CONFIG_PARKING_CAPACITY);
	zassert_equal(sessions_add("IOP5D66", 5), -ENOSPC);
}

ZTEST(sessions, test_remove_libera_vaga)
{
	zassert_ok(sessions_add("ABC1234", 1));
	zassert_ok(sessions_add("XYZ9A88", 2));
	zassert_ok(sessions_add("QWE4B21", 3));
	zassert_ok(sessions_add("RTY7C33", 4));

	zassert_ok(sessions_remove("ABC1234"));
	zassert_false(sessions_has("ABC1234"));
	zassert_equal(sessions_count(), 3);
	zassert_equal(sessions_remove("ABC1234"), -ENOENT);
	zassert_equal(sessions_remove(NULL), -EINVAL);
	zassert_false(sessions_full());
	zassert_ok(sessions_add("IOP5D66", 6));
	zassert_equal(sessions_count(), 4);
}

ZTEST(sessions, test_reset)
{
	zassert_ok(sessions_add("ABC1D23", 1));
	sessions_reset();
	zassert_equal(sessions_count(), 0);
	zassert_false(sessions_has("ABC1D23"));
}

ZTEST(sessions, test_instante_com_placa_invalida)
{
	int64_t entry = 1234;

	zassert_ok(sessions_add("ABC1D23", 500));
	zassert_equal(sessions_entry_time(NULL, &entry), -EINVAL);
	zassert_equal(sessions_entry_time("ABC1D2", &entry), -EINVAL);
	zassert_equal(sessions_entry_time("ABC1D234", &entry), -EINVAL,
		      "prefixo de placa existente nao conta");
	zassert_equal(entry, 1234, "erro nao escreve o instante");
	zassert_ok(sessions_entry_time("ABC1D23", &entry));
	zassert_equal(entry, 500);
}
