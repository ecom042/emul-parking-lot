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

struct services_exit_io {
	int (*send)(const uint8_t *data, size_t len);
	int (*show_plate)(const char plate[PARKING_PLATE_SIZE]);
	int (*tariff)(int64_t entered_ms, int64_t now_ms, uint32_t *cents);
	int (*show_amount)(uint32_t cents);
	int64_t (*now)(void);
	void (*wake)(void);
};

/* Table registered with parking_register_services(). Its callbacks run in the
 * controller thread: they only copy, take a short spinlock and wake the worker.
 */
extern const struct parking_services parking_services;

/* Clear sessions, requests and parsers. Call before registering the table. */
void services_init(const struct services_io *io);

/* Camera worker context only. */
void services_receive(enum parking_lane_id lane, uint8_t byte, int64_t now_ms);
void services_camera_resync(enum parking_lane_id lane);
void services_process(int64_t now_ms);

/* Install exit callbacks before servicing controller requests. */
void services_exit_init(const struct services_exit_io *ops);
/* Exit worker context only. RX loss is reported after transport resynchronization. */
void services_exit_receive(uint8_t byte, int64_t received_ms);
void services_exit_resync(void);
void services_exit_process(void);

/* Start the camera UARTs and the protocol worker, then register the table. */
int parking_services_start(void);
int parking_exit_start(void);

#endif
