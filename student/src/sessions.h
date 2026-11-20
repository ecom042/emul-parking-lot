#ifndef PARKING_SESSIONS_H
#define PARKING_SESSIONS_H

#include <stdint.h>

#include "parking.h"

enum session_state { SESSION_FREE, SESSION_RESERVED, SESSION_ACTIVE };

struct session {
	enum session_state state;
	uint32_t transaction;
	int64_t entered_ms;
	char plate[PARKING_PLATE_SIZE];
};

/* Bounded RAM table, cleared by machine reset. Reservations count against
 * capacity so two concurrent approvals cannot overfill the lot. Not thread
 * safe: the caller serializes access.
 */
struct sessions {
	struct session slots[CONFIG_PARKING_CAPACITY];
};

void sessions_init(struct sessions *table);

/* Reserve a spot for an entry transaction. -EEXIST if the plate is already
 * reserved or parked, -ENOSPC if the lot is full, -EINVAL for bad arguments.
 */
int sessions_reserve(struct sessions *table, const char *plate, uint32_t transaction);

/* Turn the reservation into an active session at completed passage. -ENOENT if
 * there is no reservation for this transaction; the table is left intact.
 */
int sessions_commit(struct sessions *table, uint32_t transaction, int64_t when_ms);

/* Release a reservation (not an active session). No effect if absent. */
void sessions_release(struct sessions *table, uint32_t transaction);

/* Active session for a plate, or NULL. */
const struct session *sessions_find(struct sessions *table, const char *plate);

/* Remove an active session at completed exit passage. -ENOENT if absent. */
int sessions_remove(struct sessions *table, const char *plate);

#endif
