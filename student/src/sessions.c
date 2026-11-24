/*
 * Tabela de sessões: um vetor fixo de CONFIG_PARKING_CAPACITY posições.
 * Uma posição com used == false está livre. Sem alocação dinâmica.
 */
#include "sessions.h"

#include <errno.h>
#include <string.h>

#include "parking.h"

struct session {
	bool used;
	char plate[PLATE_LEN + 1];
	int64_t entry_ms;
};

static struct session table[CONFIG_PARKING_CAPACITY];

/** Índice da placa na tabela, ou -1. */
static int find(const char *plate)
{
	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		if (table[i].used && strcmp(table[i].plate, plate) == 0) {
			return i;
		}
	}
	return -1;
}

void sessions_reset(void)
{
	memset(table, 0, sizeof(table));
}

size_t sessions_count(void)
{
	size_t n = 0;

	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		n += table[i].used ? 1U : 0U;
	}
	return n;
}

bool sessions_has_space(void)
{
	return sessions_count() < (size_t)CONFIG_PARKING_CAPACITY;
}

bool sessions_contains(const char *plate)
{
	return find(plate) >= 0;
}

int sessions_add(const char *plate, int64_t now_ms)
{
	if (find(plate) >= 0) {
		return -EEXIST;
	}
	for (int i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		if (!table[i].used) {
			table[i].used = true;
			strncpy(table[i].plate, plate, PLATE_LEN);
			table[i].plate[PLATE_LEN] = '\0';
			table[i].entry_ms = now_ms;
			return 0;
		}
	}
	return -ENOSPC;
}

int sessions_entry_time(const char *plate, int64_t *entry_ms)
{
	int i = find(plate);

	if (i < 0) {
		return -ENOENT;
	}
	*entry_ms = table[i].entry_ms;
	return 0;
}

int sessions_remove(const char *plate)
{
	int i = find(plate);

	if (i < 0) {
		return -ENOENT;
	}
	table[i].used = false;
	return 0;
}
