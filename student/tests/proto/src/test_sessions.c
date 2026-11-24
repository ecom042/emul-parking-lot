/*
 * Testes da tabela de sessões (sessions.c, REQ-04).
 */
#include <errno.h>
#include <stdio.h>

#include <zephyr/ztest.h>

#include "sessions.h"

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	sessions_clear();
}

ZTEST_SUITE(sessions, NULL, NULL, before, NULL, NULL);

ZTEST(sessions, test_add_find_remove)
{
	int64_t t = 0;

	zassert_equal(sessions_count(), 0);
	zassert_false(sessions_find("ABC1D23", &t));
	zassert_ok(sessions_add("ABC1D23", 1234));
	zassert_true(sessions_find("ABC1D23", &t));
	zassert_equal(t, 1234);
	zassert_equal(sessions_count(), 1);
	zassert_ok(sessions_remove("ABC1D23"));
	zassert_false(sessions_find("ABC1D23", &t));
	zassert_equal(sessions_count(), 0);
}

ZTEST(sessions, test_duplicate_is_refused)
{
	zassert_ok(sessions_add("ABC1D23", 0));
	zassert_equal(sessions_add("ABC1D23", 5), -EEXIST);
	zassert_equal(sessions_count(), 1);
}

ZTEST(sessions, test_full_lot)
{
	char plate[PLATE_BUF];

	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		zassert_true(sessions_has_space());
		snprintf(plate, sizeof(plate), "ABC1D%02d", i);
		zassert_ok(sessions_add(plate, i));
	}
	zassert_false(sessions_has_space());
	zassert_equal(sessions_add("XYZ9Z99", 0), -ENOSPC);
	zassert_equal(sessions_count(), CONFIG_PARKING_CAPACITY);

	/* Uma saída libera a vaga. */
	zassert_ok(sessions_remove("ABC1D00"));
	zassert_true(sessions_has_space());
	zassert_ok(sessions_add("XYZ9Z99", 0));
}

ZTEST(sessions, test_remove_unknown)
{
	zassert_equal(sessions_remove("ABC1D23"), -ENOENT);
}

ZTEST(sessions, test_clear)
{
	zassert_ok(sessions_add("ABC1D23", 0));
	sessions_clear();
	zassert_equal(sessions_count(), 0);
}
