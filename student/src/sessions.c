/*
 * Tabela de sessões (REQ-04). Ver sessions.h.
 */
#include "sessions.h"

#include <errno.h>
#include <string.h>

struct session {
	bool used;
	char plate[PLATE_BUF];
	int64_t entry_ms;
};

static struct session table[CONFIG_PARKING_CAPACITY];

void sessions_clear(void)
{
	memset(table, 0, sizeof(table));
}

int sessions_count(void)
{
	int n = 0;

	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		if (table[i].used) {
			n++;
		}
	}
	return n;
}

bool sessions_has_space(void)
{
	return sessions_count() < CONFIG_PARKING_CAPACITY;
}

static struct session *lookup(const char *plate)
{
	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		if (table[i].used && strcmp(table[i].plate, plate) == 0) {
			return &table[i];
		}
	}
	return NULL;
}

bool sessions_find(const char *plate, int64_t *entry_ms)
{
	const struct session *s = lookup(plate);

	if (s == NULL) {
		return false;
	}
	*entry_ms = s->entry_ms;
	return true;
}

int sessions_add(const char *plate, int64_t entry_ms)
{
	if (lookup(plate) != NULL) {
		return -EEXIST;
	}
	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		if (!table[i].used) {
			table[i].used = true;
			strncpy(table[i].plate, plate, PLATE_LEN);
			table[i].plate[PLATE_LEN] = '\0';
			table[i].entry_ms = entry_ms;
			return 0;
		}
	}
	return -ENOSPC;
}

int sessions_remove(const char *plate)
{
	struct session *s = lookup(plate);

	if (s == NULL) {
		return -ENOENT;
	}
	memset(s, 0, sizeof(*s));
	return 0;
}
