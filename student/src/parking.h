#ifndef PARKING_H
#define PARKING_H

#include <stdint.h>

#define PARKING_PLATE_SIZE 8

enum parking_lane_id { PARKING_ENTRY, PARKING_EXIT, PARKING_LANE_COUNT };

enum parking_request_kind {
	PARKING_CAPTURE,
	PARKING_AUTHORIZE_ENTRY,
	PARKING_AUTHORIZE_EXIT,
};

enum parking_reply_kind {
	PARKING_PLATE,
	PARKING_NO_PLATE,
	PARKING_ENTRY_ALLOWED,
	PARKING_EXIT_PAID,
	PARKING_DENIED,
};

struct parking_request {
	enum parking_request_kind kind;
	enum parking_lane_id lane;
	uint32_t transaction;
	unsigned int attempt;
	int64_t deadline_ms;
	char plate[PARKING_PLATE_SIZE];
};

struct parking_reply {
	enum parking_reply_kind kind;
	enum parking_lane_id lane;
	uint32_t transaction;
	unsigned int attempt;
	char plate[PARKING_PLATE_SIZE];
};

/* Callbacks run in the controller thread: no I/O or waits on worker locks.
 * Run protocol workers at preemptible priorities >= 1. Requests are borrowed: copy
 * them before returning. All times are Zephyr uptime in milliseconds.
 */
struct parking_services {
	/* Queue device work. For each capture attempt use a new wire ID and report a
	 * matching, validated plate with confidence >= 85 or PARKING_NO_PLATE
	 * for a matching low-confidence result. Discard malformed or stale frames.
	 * Authorization replies use attempt = 0. Entry success reserves a spot
	 * for this transaction. Exit success requires a known session and exact PAID.
	 */
	int (*request)(const struct parking_request *request);

	/* Update the bounded RAM session table once, at completed passage. On failure
	 * leave it intact. An entry reservation becomes a session at when_ms.
	 */
	int (*passage)(enum parking_lane_id lane, uint32_t transaction, int64_t when_ms);

	/* Invalidate the transaction and release any reservation immediately. Workers
	 * must recheck validity before making changes. Defer UART cancellation to the
	 * protocol worker. Safe after a failed request.
	 */
	void (*cancel)(enum parking_lane_id lane, uint32_t transaction);
};

/* Register once with a table valid until reset. Returns a negative errno on failure. */
int parking_register_services(const struct parking_services *services);

/* Copy a reply to the bounded inbox without waiting. On -ENOMSG retry from the
 * service worker while the request is current and before its deadline.
 */
int parking_post_reply(const struct parking_reply *reply);

#endif
