/*
 * Tests of services/sessions: the shared table of parked cars. No fakes: the
 * module only uses a kernel mutex. Every test starts from an empty lot.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/ztest.h>

#include "services/sessions.h"
#include "services/wire.h" /* PLATE_LEN */

#define PLATE         "ABC1D23"
#define SIMILAR_PLATE "ABC1D24" /* differs in the last character only */
#define ENTRY_MS      2500
#define UNSET_MS      (-1)

/*
 * A distinct valid plate per spot index: LOT1A00, LOT1A01... The prefix
 * differs from PLATE, so a full lot never already holds PLATE.
 */
static void plate_for_spot(int index, char plate[PLATE_LEN + 1])
{
	snprintk(plate, PLATE_LEN + 1, "LOT1A0%d", index);
}

static void fill_the_lot(void)
{
	char plate[PLATE_LEN + 1];

	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		plate_for_spot(i, plate);
		zassert_ok(sessions_create(plate, ENTRY_MS + i), "spot %d", i);
	}
}

static void empty_lot(void *fixture)
{
	ARG_UNUSED(fixture);
	sessions_clear();
}

ZTEST_SUITE(sessions, NULL, NULL, empty_lot, NULL, NULL);

ZTEST(sessions, test_empty_lot_lets_a_new_plate_enter)
{
	zassert_true(sessions_can_enter(PLATE));
}

ZTEST(sessions, test_created_session_keeps_its_entry_time)
{
	int64_t entry_ms = UNSET_MS;

	zassert_ok(sessions_create(PLATE, ENTRY_MS));

	zassert_ok(sessions_entry_time(PLATE, &entry_ms));
	zassert_equal(entry_ms, ENTRY_MS);
}

ZTEST(sessions, test_parked_plate_cannot_enter_again)
{
	zassert_ok(sessions_create(PLATE, ENTRY_MS));

	zassert_false(sessions_can_enter(PLATE));
	zassert_equal(sessions_create(PLATE, ENTRY_MS), -EEXIST);
}

ZTEST(sessions, test_similar_plates_are_different_sessions)
{
	int64_t entry_ms = UNSET_MS;

	zassert_ok(sessions_create(PLATE, ENTRY_MS));

	zassert_true(sessions_can_enter(SIMILAR_PLATE));
	zassert_equal(sessions_entry_time(SIMILAR_PLATE, &entry_ms), -ENOENT);
	zassert_equal(entry_ms, UNSET_MS, "untouched on error");
}

ZTEST(sessions, test_full_lot_refuses_a_new_plate)
{
	fill_the_lot();

	zassert_false(sessions_can_enter(PLATE));
	zassert_equal(sessions_create(PLATE, ENTRY_MS), -ENOSPC);
}

ZTEST(sessions, test_full_lot_with_the_same_plate_reports_exists)
{
	char parked[PLATE_LEN + 1];

	fill_the_lot();
	plate_for_spot(0, parked);

	zassert_equal(sessions_create(parked, ENTRY_MS), -EEXIST, "duplicate wins");
}

ZTEST(sessions, test_every_spot_keeps_its_own_entry_time)
{
	char plate[PLATE_LEN + 1];
	int64_t entry_ms;

	fill_the_lot();

	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		plate_for_spot(i, plate);
		zassert_ok(sessions_entry_time(plate, &entry_ms));
		zassert_equal(entry_ms, ENTRY_MS + i, "spot %d", i);
	}
}

ZTEST(sessions, test_removing_frees_the_spot)
{
	char leaving[PLATE_LEN + 1];

	fill_the_lot();
	plate_for_spot(1, leaving);

	zassert_ok(sessions_remove(leaving));

	zassert_true(sessions_can_enter(PLATE));
	zassert_ok(sessions_create(PLATE, ENTRY_MS));
}

ZTEST(sessions, test_removed_plate_may_enter_again)
{
	int64_t entry_ms;

	zassert_ok(sessions_create(PLATE, ENTRY_MS));
	zassert_ok(sessions_remove(PLATE));

	zassert_equal(sessions_entry_time(PLATE, &entry_ms), -ENOENT);
	zassert_true(sessions_can_enter(PLATE));
}

ZTEST(sessions, test_removing_an_unknown_plate_fails)
{
	zassert_ok(sessions_create(PLATE, ENTRY_MS));

	zassert_equal(sessions_remove(SIMILAR_PLATE), -ENOENT);
	zassert_false(sessions_can_enter(PLATE), "the parked car is untouched");
}

ZTEST(sessions, test_clear_empties_the_lot)
{
	fill_the_lot();

	sessions_clear();

	zassert_true(sessions_can_enter(PLATE));
}
