#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "camera.h"
#include "services.h"
#include "sessions.h"

enum { REQUEST_CAPACITY = 4 };

struct capture {
	bool active;
	uint32_t transaction;
	unsigned int attempt;
	uint32_t wire_id;
	int64_t deadline_ms;
};

struct outbox {
	bool full;
	int64_t deadline_ms;
	struct parking_reply reply;
};

K_MSGQ_DEFINE(service_requests, sizeof(struct parking_request), REQUEST_CAPACITY, 4);

/* lock guards current[] and the session table; the rest is worker-owned. */
static struct k_spinlock lock;
static uint32_t current[PARKING_LANE_COUNT];
static struct sessions table;

static const struct services_io *io;
static uint32_t last_wire_id;
static struct camera_parser parsers[PARKING_LANE_COUNT];
static struct capture captures[PARKING_LANE_COUNT];
static struct outbox outboxes[PARKING_LANE_COUNT];

static bool lane_valid(enum parking_lane_id lane)
{
	return (unsigned int)lane < PARKING_LANE_COUNT;
}

static int request(const struct parking_request *req)
{
	if (req == NULL || !lane_valid(req->lane) || req->transaction == 0) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&lock);
	int rc = k_msgq_put(&service_requests, req, K_NO_WAIT);

	if (rc == 0) {
		current[req->lane] = req->transaction;
	}
	k_spin_unlock(&lock, key);
	if (rc == 0) {
		io->wake();
	}
	return rc;
}

static int passage(enum parking_lane_id lane, uint32_t transaction, int64_t when_ms)
{
	if (!lane_valid(lane) || transaction == 0) {
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&lock);
	int rc = -ESRCH;

	if (current[lane] == transaction) {
		/* Exit passage removes the paid session once payment exists. */
		rc = lane == PARKING_ENTRY ? sessions_commit(&table, transaction, when_ms) :
					     -ENOTSUP;
	}
	if (rc == 0) {
		current[lane] = 0;
	}
	k_spin_unlock(&lock, key);
	return rc;
}

static void cancel(enum parking_lane_id lane, uint32_t transaction)
{
	if (!lane_valid(lane) || transaction == 0) {
		return;
	}
	k_spinlock_key_t key = k_spin_lock(&lock);

	if (current[lane] == transaction) {
		current[lane] = 0;
	}
	if (lane == PARKING_ENTRY) {
		sessions_release(&table, transaction);
	}
	k_spin_unlock(&lock, key);
	io->wake();
}

const struct parking_services parking_services = {
	.request = request,
	.passage = passage,
	.cancel = cancel,
};

void services_init(const struct services_io *ops)
{
	io = ops;
	k_msgq_purge(&service_requests);
	k_spinlock_key_t key = k_spin_lock(&lock);

	memset(current, 0, sizeof(current));
	sessions_init(&table);
	k_spin_unlock(&lock, key);
	last_wire_id = 0;
	for (int i = 0; i < PARKING_LANE_COUNT; i++) {
		camera_parser_reset(&parsers[i]);
		captures[i] = (struct capture){0};
		outboxes[i] = (struct outbox){0};
	}
}

static bool is_current(enum parking_lane_id lane, uint32_t transaction)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool valid = current[lane] == transaction;

	k_spin_unlock(&lock, key);
	return valid;
}

static void queue_reply(const struct parking_request *req, enum parking_reply_kind kind,
			const char *plate)
{
	struct outbox *box = &outboxes[req->lane];

	*box = (struct outbox){
		.full = true,
		.deadline_ms = req->deadline_ms,
		.reply = {
			.kind = kind, .lane = req->lane, .transaction = req->transaction,
			.attempt = kind == PARKING_PLATE || kind == PARKING_NO_PLATE ?
				   req->attempt : 0,
		},
	};
	if (plate != NULL) {
		memcpy(box->reply.plate, plate, PARKING_PLATE_SIZE);
	}
}

static void start_capture(const struct parking_request *req)
{
	char frame[CAMERA_FRAME_MAX];

	last_wire_id = last_wire_id >= CAMERA_ID_MAX ? 1 : last_wire_id + 1;
	captures[req->lane] = (struct capture){
		.active = true,
		.transaction = req->transaction,
		.attempt = req->attempt,
		.wire_id = last_wire_id,
		.deadline_ms = req->deadline_ms,
	};
	int len = camera_format_capture(last_wire_id, frame, sizeof(frame));

	/* A failed send leaves the capture to time out; the lane retries it. */
	(void)io->send(req->lane, (const uint8_t *)frame, (size_t)len);
}

static void authorize_entry(const struct parking_request *req)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool valid = current[req->lane] == req->transaction;
	int rc = valid ? sessions_reserve(&table, req->plate, req->transaction) : -ESRCH;

	k_spin_unlock(&lock, key);
	if (valid) {
		queue_reply(req, rc == 0 ? PARKING_ENTRY_ALLOWED : PARKING_DENIED, req->plate);
	}
}

static void handle_request(const struct parking_request *req)
{
	if (!is_current(req->lane, req->transaction)) {
		return;
	}
	switch (req->kind) {
	case PARKING_CAPTURE:
		start_capture(req);
		break;
	case PARKING_AUTHORIZE_ENTRY:
		if (req->lane == PARKING_ENTRY) {
			authorize_entry(req);
		} else {
			queue_reply(req, PARKING_DENIED, req->plate);
		}
		break;
	case PARKING_AUTHORIZE_EXIT:
		/* Exit payment is not integrated yet: never authorize an opening. */
		queue_reply(req, PARKING_DENIED, req->plate);
		break;
	}
}

void services_receive(enum parking_lane_id lane, uint8_t byte, int64_t now_ms)
{
	struct camera_plate result;

	if (!lane_valid(lane) || !camera_parser_feed(&parsers[lane], byte, &result)) {
		return;
	}
	struct capture *cap = &captures[lane];

	if (!cap->active || result.id != cap->wire_id || now_ms >= cap->deadline_ms ||
	    !is_current(lane, cap->transaction)) {
		return;
	}
	cap->active = false;
	const struct parking_request req = {
		.lane = lane, .transaction = cap->transaction,
		.attempt = cap->attempt, .deadline_ms = cap->deadline_ms,
	};
	queue_reply(&req, result.confidence >= CAMERA_CONFIDENCE_MIN ?
			  PARKING_PLATE : PARKING_NO_PLATE, result.plate);
}

static void flush(struct outbox *box, int64_t now_ms)
{
	if (!box->full) {
		return;
	}
	if (now_ms >= box->deadline_ms || !is_current(box->reply.lane, box->reply.transaction)) {
		box->full = false;
		return;
	}
	/* -ENOMSG: controller inbox full, retry on the next pass. */
	box->full = io->post(&box->reply) == -ENOMSG;
}

void services_process(int64_t now_ms)
{
	struct parking_request req;

	while (k_msgq_get(&service_requests, &req, K_NO_WAIT) == 0) {
		handle_request(&req);
	}
	for (int i = 0; i < PARKING_LANE_COUNT; i++) {
		if (captures[i].active && (now_ms >= captures[i].deadline_ms ||
					   !is_current(i, captures[i].transaction))) {
			captures[i].active = false;
		}
		flush(&outboxes[i], now_ms);
	}
}
