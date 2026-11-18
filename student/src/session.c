/*
 * session.c — Session table management
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "session.h"
#include <string.h>

static session_t sessions[SESSION_MAX_CAPACITY];

void session_init(void)
{
	memset(sessions, 0, sizeof(sessions));
}

int session_count(void)
{
	int count = 0;
	for (int i = 0; i < SESSION_MAX_CAPACITY; i++) {
		if (sessions[i].active) {
			count++;
		}
	}
	return count;
}

bool session_is_full(void)
{
	return session_count() >= SESSION_MAX_CAPACITY;
}

bool session_has_plate(const char *plate)
{
	return session_find(plate) != NULL;
}

int session_create(const char *plate, int64_t entry_time_ms)
{
	if (session_is_full()) {
		return -1;
	}
	if (session_has_plate(plate)) {
		return -1;
	}

	for (int i = 0; i < SESSION_MAX_CAPACITY; i++) {
		if (!sessions[i].active) {
			strncpy(sessions[i].plate, plate, SESSION_PLATE_LEN);
			sessions[i].plate[SESSION_PLATE_LEN] = '\0';
			sessions[i].entry_time_ms = entry_time_ms;
			sessions[i].active = true;
			return 0;
		}
	}
	return -1; /* LCOV_EXCL_LINE: should not reach if count check passed */
}

session_t *session_find(const char *plate)
{
	for (int i = 0; i < SESSION_MAX_CAPACITY; i++) {
		if (sessions[i].active &&
		    strncmp(sessions[i].plate, plate, SESSION_PLATE_LEN) == 0) {
			return &sessions[i];
		}
	}
	return NULL;
}

int session_remove(const char *plate)
{
	session_t *s = session_find(plate);
	if (s == NULL) {
		return -1;
	}
	s->active = false;
	memset(s->plate, 0, sizeof(s->plate));
	s->entry_time_ms = 0;
	return 0;
}

void session_clear(void)
{
	session_init();
}
