/*
 * session.h — Session table management
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SESSION_H
#define SESSION_H

#include <stdbool.h>
#include <stdint.h>

#define SESSION_PLATE_LEN  7
#define SESSION_MAX_CAPACITY CONFIG_PARKING_CAPACITY

typedef struct {
	char plate[SESSION_PLATE_LEN + 1];
	int64_t entry_time_ms;  /* virtual time of completed entry passage */
	bool active;
} session_t;

/* Initialize the session table (all empty). */
void session_init(void);

/* Count the number of active sessions. */
int session_count(void);

/* Check if the lot is full. */
bool session_is_full(void);

/* Check if a plate already has an active session. */
bool session_has_plate(const char *plate);

/* Create a new session for the given plate. Returns 0 on success, -1 if full. */
int session_create(const char *plate, int64_t entry_time_ms);

/* Find a session by plate. Returns pointer or NULL. */
session_t *session_find(const char *plate);

/* Remove (end) a session for the given plate. Returns 0 on success, -1 if not found. */
int session_remove(const char *plate);

/* Clear all sessions (used on reset). */
void session_clear(void);

#endif /* SESSION_H */
