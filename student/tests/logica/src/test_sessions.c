/* Tabela de sessões compartilhada pelas duas faixas. */
#include <errno.h>
#include <stdio.h>

#include <zephyr/ztest.h>

#include "sessions.h"

static void before(void *f)
{
	ARG_UNUSED(f);
	sessions_reset();
}

ZTEST_SUITE(sessions, NULL, NULL, before, NULL, NULL);

ZTEST(sessions, test_entra_e_sai)
{
	int64_t t = 0;

	zassert_false(sessions_contains("ABC1D23"));
	zassert_ok(sessions_add("ABC1D23", 1234));
	zassert_true(sessions_contains("ABC1D23"));
	zassert_equal(sessions_count(), 1);
	zassert_ok(sessions_entry_time("ABC1D23", &t));
	zassert_equal(t, 1234);
	zassert_ok(sessions_remove("ABC1D23"));
	zassert_false(sessions_contains("ABC1D23"));
	zassert_equal(sessions_count(), 0);
}

ZTEST(sessions, test_placa_repetida)
{
	zassert_ok(sessions_add("ABC1D23", 0));
	zassert_equal(sessions_add("ABC1D23", 5), -EEXIST);
	zassert_equal(sessions_count(), 1);
}

ZTEST(sessions, test_lotado)
{
	char plate[8];

	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		zassert_true(sessions_has_space());
		snprintf(plate, sizeof(plate), "AAA%04d", i);
		zassert_ok(sessions_add(plate, i));
	}
	zassert_false(sessions_has_space());
	zassert_equal(sessions_add("ZZZ9Z99", 0), -ENOSPC);
	/* Liberar uma vaga permite entrar de novo. */
	zassert_ok(sessions_remove("AAA0000"));
	zassert_true(sessions_has_space());
	zassert_ok(sessions_add("ZZZ9Z99", 0));
}

ZTEST(sessions, test_placa_sem_sessao)
{
	int64_t t = 99;

	zassert_equal(sessions_entry_time("ABC1D23", &t), -ENOENT);
	zassert_equal(t, 99);
	zassert_equal(sessions_remove("ABC1D23"), -ENOENT);
}
