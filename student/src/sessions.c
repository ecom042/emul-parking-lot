#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include "sessions.h"

static bool same_plate(const struct session *slot, const char *plate)
{
	return strncmp(slot->plate, plate, PARKING_PLATE_SIZE) == 0;
}

void sessions_init(struct sessions *table)
{
	*table = (struct sessions){0};
}

int sessions_reserve(struct sessions *table, const char *plate, uint32_t transaction)
{
	struct session *free_slot = NULL;

	if (plate == NULL || transaction == 0 || plate[PARKING_PLATE_SIZE - 1] != '\0') {
		return -EINVAL;
	}
	for (size_t i = 0; i < ARRAY_SIZE(table->slots); i++) {
		struct session *slot = &table->slots[i];

		if (slot->state == SESSION_FREE) {
			free_slot = free_slot == NULL ? slot : free_slot;
		} else if (same_plate(slot, plate) || slot->transaction == transaction) {
			return -EEXIST;
		}
	}
	if (free_slot == NULL) {
		return -ENOSPC;
	}
	*free_slot = (struct session){ .state = SESSION_RESERVED, .transaction = transaction };
	memcpy(free_slot->plate, plate, PARKING_PLATE_SIZE);
	return 0;
}

static struct session *find_reserved(struct sessions *table, uint32_t transaction)
{
	for (size_t i = 0; transaction != 0 && i < ARRAY_SIZE(table->slots); i++) {
		struct session *slot = &table->slots[i];

		if (slot->state == SESSION_RESERVED && slot->transaction == transaction) {
			return slot;
		}
	}
	return NULL;
}

int sessions_commit(struct sessions *table, uint32_t transaction, int64_t when_ms)
{
	struct session *slot = find_reserved(table, transaction);

	if (slot == NULL) {
		return -ENOENT;
	}
	slot->state = SESSION_ACTIVE;
	slot->transaction = 0;
	slot->entered_ms = when_ms;
	return 0;
}

void sessions_release(struct sessions *table, uint32_t transaction)
{
	struct session *slot = find_reserved(table, transaction);

	if (slot != NULL) {
		*slot = (struct session){0};
	}
}

static struct session *find_active(struct sessions *table, const char *plate)
{
	for (size_t i = 0; plate != NULL && i < ARRAY_SIZE(table->slots); i++) {
		struct session *slot = &table->slots[i];

		if (slot->state == SESSION_ACTIVE && same_plate(slot, plate)) {
			return slot;
		}
	}
	return NULL;
}

const struct session *sessions_find(struct sessions *table, const char *plate)
{
	return find_active(table, plate);
}

int sessions_remove(struct sessions *table, const char *plate)
{
	struct session *slot = find_active(table, plate);

	if (slot == NULL) {
		return -ENOENT;
	}
	*slot = (struct session){0};
	return 0;
}
