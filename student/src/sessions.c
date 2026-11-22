/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file sessions.c
 * @brief Tabela estatica de sessoes, sem alocacao dinamica.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "parking_limits.h"
#include "sessions.h"

BUILD_ASSERT(CONFIG_PARKING_CAPACITY >= 1, "capacidade minima 1");
BUILD_ASSERT(CONFIG_PARKING_CAPACITY <= 8, "capacidade maxima 8");

struct session {
	bool used;
	char plate[PLATE_LEN + 1];
	int64_t entry_ms;
};

static struct session table[CONFIG_PARKING_CAPACITY];

static bool plate_ok(const char *plate)
{
	return plate != NULL && strlen(plate) == PLATE_LEN;
}

static struct session *find(const char *plate)
{
	for (size_t i = 0; i < ARRAY_SIZE(table); i++) {
		if (table[i].used && strcmp(table[i].plate, plate) == 0) {
			return &table[i];
		}
	}
	return NULL;
}

void sessions_reset(void)
{
	memset(table, 0, sizeof(table));
}

bool sessions_full(void)
{
	return sessions_count() >= ARRAY_SIZE(table);
}

uint8_t sessions_count(void)
{
	uint8_t count = 0;

	for (size_t i = 0; i < ARRAY_SIZE(table); i++) {
		if (table[i].used) {
			count++;
		}
	}
	return count;
}

bool sessions_has(const char *plate)
{
	return plate_ok(plate) && find(plate) != NULL;
}

int sessions_add(const char *plate, int64_t entry_ms)
{
	if (!plate_ok(plate)) {
		return -EINVAL;
	}
	if (find(plate) != NULL) {
		return -EEXIST;
	}

	for (size_t i = 0; i < ARRAY_SIZE(table); i++) {
		if (!table[i].used) {
			table[i].used = true;
			table[i].entry_ms = entry_ms;
			memcpy(table[i].plate, plate, PLATE_LEN + 1);
			return 0;
		}
	}
	return -ENOSPC;
}

int sessions_remove(const char *plate)
{
	struct session *s;

	if (!plate_ok(plate)) {
		return -EINVAL;
	}
	s = find(plate);
	if (s == NULL) {
		return -ENOENT;
	}
	memset(s, 0, sizeof(*s));
	return 0;
}

int sessions_entry_time(const char *plate, int64_t *entry_ms)
{
	struct session *s;

	if (!plate_ok(plate) || entry_ms == NULL) {
		return -EINVAL;
	}
	s = find(plate);
	if (s == NULL) {
		return -ENOENT;
	}
	*entry_ms = s->entry_ms;
	return 0;
}
