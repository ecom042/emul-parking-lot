/*
 * session.c — Reset-scoped parking session table implementation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "session.h"

#include <errno.h>
#include <string.h>

static session_t sessions[SESSION_MAX_CAPACITY];

void session_init(void)
{
	(void)memset(sessions, 0, sizeof(sessions));
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

const session_t *session_find(const char *plate)
{
	for (int i = 0; i < SESSION_MAX_CAPACITY; i++) {
		if (sessions[i].active &&
		    strncmp(sessions[i].plate, plate, SESSION_PLATE_LEN + 1) == 0) {
			return &sessions[i];
		}
	}
	return NULL;
}

bool session_has_plate(const char *plate)
{
	return session_find(plate) != NULL;
}

int session_create(const char *plate, int64_t entry_time_ms)
{
	if (session_has_plate(plate)) {
		return -EEXIST;
	}

	for (int i = 0; i < SESSION_MAX_CAPACITY; i++) {
		if (!sessions[i].active) {
			(void)memcpy(sessions[i].plate, plate, SESSION_PLATE_LEN);
			sessions[i].plate[SESSION_PLATE_LEN] = '\0';
			sessions[i].entry_time_ms = entry_time_ms;
			sessions[i].active = true;
			return 0;
		}
	}
	return -ENOSPC;
}

int session_remove(const char *plate)
{
	for (int i = 0; i < SESSION_MAX_CAPACITY; i++) {
		if (sessions[i].active &&
		    strncmp(sessions[i].plate, plate, SESSION_PLATE_LEN + 1) == 0) {
			(void)memset(&sessions[i], 0, sizeof(sessions[i]));
			return 0;
		}
	}
	return -ENOENT;
}
