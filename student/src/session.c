/**
 * @file session.c
 * @brief Tabela de sessões, indexada pela placa.
 */

#include "session.h"

#include <errno.h>
#include <string.h>

#include "camera.h"

/** @brief Uma vaga; placa vazia indica vaga livre. */
struct session {
	char plate[CAMERA_PLATE_LEN + 1]; /**< Placa estacionada. */
	int64_t entry_ms;                 /**< Tempo virtual da entrada. */
};

static struct session sessions[CONFIG_PARKING_CAPACITY];

/** @brief Devolve a vaga com a placa, ou NULL. Com "" acha uma vaga livre. */
static struct session *lookup(const char *plate)
{
	for (size_t i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		if (strcmp(sessions[i].plate, plate) == 0) {
			return &sessions[i];
		}
	}
	return NULL;
}

int session_add(const char *plate, int64_t entry_ms)
{
	struct session *s;

	if (lookup(plate) != NULL) {
		return -EEXIST;
	}
	s = lookup("");
	if (s == NULL) {
		return -ENOSPC;
	}
	strncpy(s->plate, plate, sizeof(s->plate) - 1);
	s->entry_ms = entry_ms;
	return 0;
}

int session_find(const char *plate, int64_t *entry_ms)
{
	const struct session *s = lookup(plate);

	if (s == NULL) {
		return -ENOENT;
	}
	if (entry_ms != NULL) {
		*entry_ms = s->entry_ms;
	}
	return 0;
}

int session_remove(const char *plate)
{
	struct session *s = lookup(plate);

	if (s == NULL) {
		return -ENOENT;
	}
	s->plate[0] = '\0';
	return 0;
}

size_t session_count(void)
{
	size_t count = 0;

	for (size_t i = 0; i < CONFIG_PARKING_CAPACITY; i++) {
		count += sessions[i].plate[0] != '\0';
	}
	return count;
}
