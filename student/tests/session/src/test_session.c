/**
 * @file test_session.c
 * @brief Testes da tabela de sessões (session.c).
 */

#include <errno.h>
#include <stdio.h>

#include <zephyr/ztest.h>

#include "session.h"

/** @brief Gera uma placa válida distinta para cada índice. */
static const char *plate_of(unsigned int i)
{
	static char plate[8];

	snprintf(plate, sizeof(plate), "AAA%04u", i);
	return plate;
}

/** @brief Esvazia a tabela antes de cada teste. */
static void empty_table(void *fixture)
{
	ARG_UNUSED(fixture);
	for (unsigned int i = 0; i < 2 * CONFIG_PARKING_CAPACITY; i++) {
		int err = session_remove(plate_of(i));

		zassert_true(err == 0 || err == -ENOENT);
	}
}

ZTEST_SUITE(session, NULL, NULL, empty_table, NULL, NULL);

ZTEST(session, test_add_then_find_returns_entry_time)
{
	int64_t entry_ms = 0;

	zassert_ok(session_add("ABC1D23", 1234));
	zassert_ok(session_find("ABC1D23", &entry_ms));
	zassert_equal(entry_ms, 1234);
	zassert_ok(session_find("ABC1D23", NULL));
	zassert_equal(session_count(), 1);
	zassert_ok(session_remove("ABC1D23"));
}

ZTEST(session, test_duplicate_plate_is_rejected)
{
	zassert_ok(session_add(plate_of(0), 1));
	zassert_equal(session_add(plate_of(0), 2), -EEXIST);
	zassert_equal(session_count(), 1);
}

ZTEST(session, test_full_lot_is_rejected)
{
	for (unsigned int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		zassert_ok(session_add(plate_of(i), i));
	}
	zassert_equal(session_add(plate_of(CONFIG_PARKING_CAPACITY), 0), -ENOSPC);
	zassert_equal(session_count(), CONFIG_PARKING_CAPACITY);
}

ZTEST(session, test_remove_frees_the_spot)
{
	for (unsigned int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		zassert_ok(session_add(plate_of(i), i));
	}
	zassert_ok(session_remove(plate_of(0)));
	zassert_equal(session_find(plate_of(0), NULL), -ENOENT);
	zassert_ok(session_add(plate_of(CONFIG_PARKING_CAPACITY), 0));
}

ZTEST(session, test_unknown_plate_is_not_found)
{
	zassert_equal(session_find("ZZZ9Z99", NULL), -ENOENT);
	zassert_equal(session_remove("ZZZ9Z99"), -ENOENT);
	zassert_equal(session_count(), 0);
}
