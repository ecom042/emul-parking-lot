#ifndef PARKING_SERVICES_H
#define PARKING_SERVICES_H

#include <stddef.h>
#include <stdint.h>

#include "parking.h"

/* Device operations used by the protocol worker. post has the semantics of
 * parking_post_reply (-ENOMSG when the controller inbox is full).
 */
struct services_io {
	int (*send)(enum parking_lane_id lane, const uint8_t *data, size_t len);
	int (*post)(const struct parking_reply *reply);
	void (*wake)(void);
};

/* Table registered with parking_register_services(). Its callbacks run in the
 * controller thread: they only copy, take a short spinlock and wake the worker.
 */
extern const struct parking_services parking_services;

/* Clear sessions, requests and parsers. Call before registering the table. */
void services_init(const struct services_io *io);

/* Worker context only. Bytes are parsed with the time they were drained. */
void services_receive(enum parking_lane_id lane, uint8_t byte, int64_t now_ms);
void services_process(int64_t now_ms);

/* Start the camera UARTs and the protocol worker, then register the table. */
int parking_services_start(void);

#endif
