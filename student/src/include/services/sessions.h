#ifndef PARKING_SERVICES_SESSIONS_H_
#define PARKING_SERVICES_SESSIONS_H_

/*
 * Parked cars: one session per plate, with the virtual time its entry
 * passage completed, up to CONFIG_PARKING_CAPACITY at once (REQ-04). Shared
 * by both lane threads; every call is short and safe from either thread.
 * Nothing survives a reset (REQ-13). Only the entry lane creates sessions,
 * so a free spot checked when the plate is read is still free when the car
 * completes its passage.
 *
 * Every plate argument is a valid, NUL-terminated plate (from the camera)
 * and never NULL.
 */

#include <stdbool.h>
#include <stdint.h>

/** @brief Forget every session: an empty lot. Called at startup. */
void sessions_clear(void);

/**
 * @brief Whether this plate may enter: a spot is free and the plate has no
 *        session yet.
 */
bool sessions_can_enter(const char *plate);

/**
 * @brief Park a car: record its plate and entry time.
 * Call only when the entry passage completes (REQ-07).
 * @param entry_ms virtual time of the completed entry passage.
 * @retval 0 on success.
 * @retval -EEXIST the plate is already parked.
 * @retval -ENOSPC the lot is full.
 */
int sessions_create(const char *plate, int64_t entry_ms);

/**
 * @brief The entry time of a parked car (the start of its tariff).
 * @param[out] entry_ms the time given to sessions_create(); untouched on error.
 * @retval 0 on success.
 * @retval -ENOENT the plate has no session.
 */
int sessions_entry_time(const char *plate, int64_t *entry_ms);

/**
 * @brief End a car's session and free its spot.
 * Call only when the exit passage completes (REQ-07).
 * @retval 0 on success.
 * @retval -ENOENT the plate has no session.
 */
int sessions_remove(const char *plate);

#endif /* PARKING_SERVICES_SESSIONS_H_ */
