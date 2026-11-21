#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "camera.h"
#include "payment.h"
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
K_MSGQ_DEFINE(exit_requests, sizeof(struct parking_request), REQUEST_CAPACITY, 4);

/* lock guards current[], paid_exit and the session table. Each worker owns its parser. */
static struct k_spinlock lock;
static uint32_t current[PARKING_LANE_COUNT];
static struct sessions table;
static struct {
	uint32_t transaction;
	char plate[PARKING_PLATE_SIZE];
} paid_exit;

static const struct services_io *io;
static uint32_t last_wire_id;
static struct camera_parser parsers[PARKING_LANE_COUNT];
static struct capture captures[PARKING_LANE_COUNT];
static struct outbox outboxes[PARKING_LANE_COUNT];

static const struct services_exit_io *exit_io;
static struct parking_request exit_request;
static struct outbox exit_outbox;
static struct payment payment;

static bool lane_valid(enum parking_lane_id lane)
{
	return (unsigned int)lane < PARKING_LANE_COUNT;
}

static int request(const struct parking_request *req)
{
	if (req == NULL || !lane_valid(req->lane) || req->transaction == 0) {
		return -EINVAL;
	}
	bool use_exit_worker = exit_io != NULL && req->lane == PARKING_EXIT &&
			       req->kind == PARKING_AUTHORIZE_EXIT;
	k_spinlock_key_t key = k_spin_lock(&lock);
	struct k_msgq *queue = use_exit_worker ? &exit_requests : &service_requests;
	int rc = k_msgq_put(queue, req, K_NO_WAIT);

	if (rc == 0) {
		current[req->lane] = req->transaction;
	}
	k_spin_unlock(&lock, key);
	if (rc == 0) {
		if (use_exit_worker) {
			exit_io->wake();
		} else {
			io->wake();
		}
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
		if (lane == PARKING_ENTRY) {
			rc = sessions_commit(&table, transaction, when_ms);
		} else if (paid_exit.transaction == transaction) {
			rc = sessions_remove(&table, paid_exit.plate);
			if (rc == 0) {
				paid_exit.transaction = 0;
			}
		} else {
			rc = -EACCES;
		}
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
	} else if (paid_exit.transaction == transaction) {
		paid_exit.transaction = 0;
	}
	k_spin_unlock(&lock, key);
	io->wake();
	if (lane == PARKING_EXIT && exit_io != NULL) {
		exit_io->wake();
	}
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
	k_msgq_purge(&exit_requests);
	exit_io = NULL;
	k_spinlock_key_t key = k_spin_lock(&lock);

	memset(current, 0, sizeof(current));
	paid_exit.transaction = 0;
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

static void fill_reply(struct outbox *box, const struct parking_request *req,
		       enum parking_reply_kind kind, const char *plate)
{
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

static void queue_reply(const struct parking_request *req, enum parking_reply_kind kind,
			const char *plate)
{
	fill_reply(&outboxes[req->lane], req, kind, plate);
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
		queue_reply(req, PARKING_DENIED, req->plate);
		break;
	}
}

void services_receive(enum parking_lane_id lane, uint8_t byte, int64_t now_ms)
{
	struct camera_plate result;

	if (!lane_valid(lane) || !camera_parser_feed(&parsers[lane], byte, now_ms, &result)) {
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

void services_camera_resync(enum parking_lane_id lane)
{
	camera_parser_reset(&parsers[lane]);
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

void services_exit_init(const struct services_exit_io *ops)
{
	exit_io = ops;
	exit_request = (struct parking_request){0};
	exit_outbox = (struct outbox){0};
	payment_init(&payment, ops->send);
}

static void finish_payment(enum payment_event event)
{
	if (event == PAYMENT_NONE || exit_request.transaction == 0) {
		return;
	}
	uint32_t transaction = exit_request.transaction;
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool valid = current[PARKING_EXIT] == transaction;

	if (valid && event == PAYMENT_PAID) {
		paid_exit.transaction = transaction;
		memcpy(paid_exit.plate, exit_request.plate, sizeof(paid_exit.plate));
	}
	k_spin_unlock(&lock, key);
	if (valid) {
		fill_reply(&exit_outbox, &exit_request,
			   event == PAYMENT_PAID ? PARKING_EXIT_PAID : PARKING_DENIED, NULL);
	}
	exit_request.transaction = 0;
}

static bool exit_current(int64_t now_ms)
{
	return now_ms < exit_request.deadline_ms &&
	       is_current(PARKING_EXIT, exit_request.transaction);
}

static void begin_payment(const struct parking_request *req)
{
	if (!is_current(PARKING_EXIT, req->transaction)) {
		return;
	}
	if (exit_request.transaction != 0) {
		payment_cancel(&payment);
	}
	int64_t entered_ms = 0;
	k_spinlock_key_t key = k_spin_lock(&lock);
	const struct session *session = sessions_find(&table, req->plate);
	bool found = session != NULL;

	if (found) {
		entered_ms = session->entered_ms;
	}
	k_spin_unlock(&lock, key);

	exit_request = *req;
	if (!exit_current(exit_io->now())) {
		exit_request.transaction = 0;
		return;
	}
	uint32_t cents;

	if (!found || exit_io->show_plate(req->plate) != 0 ||
	    exit_io->tariff(entered_ms, exit_io->now(), &cents) != 0 ||
	    exit_io->show_amount(cents) != 0) {
		finish_payment(PAYMENT_DENIED);
		return;
	}
	int64_t now_ms = exit_io->now();

	if (!exit_current(now_ms)) {
		exit_request.transaction = 0;
		return;
	}
	if (payment_begin(&payment, req->plate, cents, now_ms) != 0) {
		finish_payment(PAYMENT_DENIED);
	}
}

void services_exit_receive(uint8_t byte, int64_t received_ms)
{
	if (exit_request.transaction != 0 && !exit_current(exit_io->now())) {
		payment_cancel(&payment);
		exit_request.transaction = 0;
	}
	finish_payment(payment_receive(&payment, byte, received_ms));
}

void services_exit_resync(void)
{
	payment_rx_lost(&payment);
}

void services_exit_process(void)
{
	int64_t now_ms = exit_io->now();

	if (exit_request.transaction != 0 && !exit_current(now_ms)) {
		payment_cancel(&payment);
		exit_request.transaction = 0;
	}
	struct parking_request req;

	while (k_msgq_get(&exit_requests, &req, K_NO_WAIT) == 0) {
		begin_payment(&req);
	}
	now_ms = exit_io->now();
	finish_payment(payment_poll(&payment, now_ms));
	flush(&exit_outbox, now_ms);
}
