/*
 * session.h — Reset-scoped parking session table (REQ-04, REQ-13)
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PARKING_SESSION_H_
#define PARKING_SESSION_H_

#include <stdbool.h>
#include <stdint.h>

#ifndef CONFIG_PARKING_CAPACITY
#define CONFIG_PARKING_CAPACITY 4
#endif

#define SESSION_PLATE_LEN    7
#define SESSION_MAX_CAPACITY CONFIG_PARKING_CAPACITY

typedef struct {
	char plate[SESSION_PLATE_LEN + 1];
	int64_t entry_time_ms;
	bool active;
} session_t;

void session_init(void);
int session_count(void);
bool session_is_full(void);
bool session_has_plate(const char *plate);
const session_t *session_find(const char *plate);
int session_create(const char *plate, int64_t entry_time_ms);
int session_remove(const char *plate);

#endif /* PARKING_SESSION_H_ */
