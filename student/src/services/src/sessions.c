#include "services/sessions.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "services/wire.h" /* PLATE_LEN */

/* One parking spot. */
struct session {
	char plate[PLATE_LEN + 1]; /**< NUL-terminated plate */
	int64_t entry_ms;          /**< virtual time of the completed entry passage */
	bool used;                 /**< false: the spot is free */
};

/* The lot, shared by both lane threads: only touched with `lock` held. */
static struct {
	struct session spots[CONFIG_PARKING_CAPACITY];
} self;

static K_MUTEX_DEFINE(lock);

/*
 * The *_locked helpers expect `lock` held. Each public function locks, calls
 * one helper and unlocks: the critical section only searches or writes the
 * table, nothing slow.
 */

static struct session *find_session_locked(const char *plate)
{
	for (size_t i = 0; i < ARRAY_SIZE(self.spots); i++) {
		struct session *spot = &self.spots[i];

		if (spot->used && strcmp(spot->plate, plate) == 0) {
			return spot;
		}
	}
	return NULL;
}

static struct session *find_free_spot_locked(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(self.spots); i++) {
		if (!self.spots[i].used) {
			return &self.spots[i];
		}
	}
	return NULL;
}

static int create_locked(const char *plate, int64_t entry_ms)
{
	if (find_session_locked(plate) != NULL) {
		return -EEXIST;
	}

	struct session *spot = find_free_spot_locked();

	if (spot == NULL) {
		return -ENOSPC;
	}
	snprintk(spot->plate, sizeof(spot->plate), "%s", plate);
	spot->entry_ms = entry_ms;
	spot->used = true;
	return 0;
}

static int entry_time_locked(const char *plate, int64_t *entry_ms)
{
	const struct session *spot = find_session_locked(plate);

	if (spot == NULL) {
		return -ENOENT;
	}
	*entry_ms = spot->entry_ms;
	return 0;
}

static int remove_locked(const char *plate)
{
	struct session *spot = find_session_locked(plate);

	if (spot == NULL) {
		return -ENOENT;
	}
	spot->used = false;
	return 0;
}

/* With K_FOREVER, k_mutex_lock() cannot fail. */
static void lock_table(void)
{
	(void)k_mutex_lock(&lock, K_FOREVER);
}

static void unlock_table(void)
{
	(void)k_mutex_unlock(&lock);
}

void sessions_clear(void)
{
	lock_table();
	memset(&self, 0, sizeof(self));
	unlock_table();
}

bool sessions_can_enter(const char *plate)
{
	lock_table();
	bool can_enter = (find_session_locked(plate) == NULL) &&
			 (find_free_spot_locked() != NULL);
	unlock_table();
	return can_enter;
}

int sessions_create(const char *plate, int64_t entry_ms)
{
	lock_table();
	int err = create_locked(plate, entry_ms);
	unlock_table();
	return err;
}

int sessions_entry_time(const char *plate, int64_t *entry_ms)
{
	lock_table();
	int err = entry_time_locked(plate, entry_ms);
	unlock_table();
	return err;
}

int sessions_remove(const char *plate)
{
	lock_table();
	int err = remove_locked(plate);
	unlock_table();
	return err;
}
