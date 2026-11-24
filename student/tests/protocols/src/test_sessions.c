#include <errno.h>
#include <stdio.h>

#include <zephyr/ztest.h>

#include "sessions.h"

static struct sessions table;

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	sessions_init(&table);
}

static void plate_for(unsigned int i, char plate[PARKING_PLATE_SIZE])
{
	snprintf(plate, PARKING_PLATE_SIZE, "ABC1D%02u", i);
}

ZTEST(sessions, test_reserve_then_commit_creates_session)
{
	zassert_ok(sessions_reserve(&table, "ABC1D23", 7));
	zassert_is_null(sessions_find(&table, "ABC1D23"), "reservation is not a session");
	zassert_ok(sessions_commit(&table, 7, 1234));

	const struct session *s = sessions_find(&table, "ABC1D23");

	zassert_not_null(s);
	zassert_equal(s->entered_ms, 1234);
	zassert_equal(s->state, SESSION_ACTIVE);
}

ZTEST(sessions, test_duplicate_plate_is_rejected)
{
	zassert_ok(sessions_reserve(&table, "ABC1D23", 1));
	zassert_equal(sessions_reserve(&table, "ABC1D23", 2), -EEXIST);
	zassert_ok(sessions_commit(&table, 1, 10));
	zassert_equal(sessions_reserve(&table, "ABC1D23", 3), -EEXIST);
	/* After commit the transaction is no longer bound to the slot. */
	zassert_ok(sessions_reserve(&table, "XYZ9A00", 1));
}

ZTEST(sessions, test_same_transaction_cannot_reserve_twice)
{
	zassert_ok(sessions_reserve(&table, "ABC1D23", 5));
	zassert_equal(sessions_reserve(&table, "XYZ9A00", 5), -EEXIST);
}

ZTEST(sessions, test_capacity_counts_reservations)
{
	char plate[PARKING_PLATE_SIZE];
	unsigned int i;

	for (i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		plate_for(i, plate);
		zassert_ok(sessions_reserve(&table, plate, i + 1));
		if (i % 2 == 0) {
			zassert_ok(sessions_commit(&table, i + 1, i));
		}
	}
	plate_for(i, plate);
	zassert_equal(sessions_reserve(&table, plate, i + 1), -ENOSPC);

	/* Releasing a reservation frees its spot. */
	sessions_release(&table, 2);
	zassert_ok(sessions_reserve(&table, plate, i + 1));
}

ZTEST(sessions, test_release_only_affects_reservations)
{
	zassert_ok(sessions_reserve(&table, "ABC1D23", 1));
	zassert_ok(sessions_commit(&table, 1, 0));
	sessions_release(&table, 1);
	sessions_release(&table, 99);
	sessions_release(&table, 0);
	zassert_not_null(sessions_find(&table, "ABC1D23"));
}

ZTEST(sessions, test_commit_failure_leaves_table_intact)
{
	zassert_equal(sessions_commit(&table, 1, 0), -ENOENT);
	zassert_equal(sessions_commit(&table, 0, 0), -ENOENT);
	zassert_ok(sessions_reserve(&table, "ABC1D23", 1));
	zassert_ok(sessions_commit(&table, 1, 0));
	zassert_equal(sessions_commit(&table, 1, 5), -ENOENT, "commit is once");
	zassert_equal(sessions_find(&table, "ABC1D23")->entered_ms, 0);
}

ZTEST(sessions, test_remove_frees_spot)
{
	zassert_equal(sessions_remove(&table, "ABC1D23"), -ENOENT);
	zassert_ok(sessions_reserve(&table, "ABC1D23", 1));
	zassert_equal(sessions_remove(&table, "ABC1D23"), -ENOENT, "reserved is not active");
	zassert_ok(sessions_commit(&table, 1, 0));
	zassert_ok(sessions_remove(&table, "ABC1D23"));
	zassert_is_null(sessions_find(&table, "ABC1D23"));
	zassert_ok(sessions_reserve(&table, "ABC1D23", 2));
}

ZTEST(sessions, test_invalid_arguments)
{
	const char unterminated[PARKING_PLATE_SIZE] = { 'A', 'B', 'C', '1', 'D', '2', '3', '4' };

	zassert_equal(sessions_reserve(&table, NULL, 1), -EINVAL);
	zassert_equal(sessions_reserve(&table, "ABC1D23", 0), -EINVAL);
	zassert_equal(sessions_reserve(&table, unterminated, 1), -EINVAL);
	zassert_is_null(sessions_find(&table, NULL));
}

ZTEST_SUITE(sessions, NULL, NULL, before_each, NULL, NULL);
