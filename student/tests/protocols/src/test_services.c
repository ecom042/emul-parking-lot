#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "services.h"
#include "payment.h"

DEFINE_FFF_GLOBALS;

FAKE_VALUE_FUNC(int, fake_send, enum parking_lane_id, const uint8_t *, size_t);
FAKE_VALUE_FUNC(int, fake_post, const struct parking_reply *);
FAKE_VOID_FUNC(fake_wake);
FAKE_VALUE_FUNC(int, fake_terminal_send, const uint8_t *, size_t);
FAKE_VALUE_FUNC(int, fake_show_plate, const char *);
FAKE_VALUE_FUNC(int, fake_tariff, int64_t, int64_t, uint32_t *);
FAKE_VALUE_FUNC(int, fake_show_amount, uint32_t);
FAKE_VALUE_FUNC(int64_t, fake_now);

static const struct services_io io = { .send = fake_send, .post = fake_post, .wake = fake_wake };
static const struct parking_services *svc = &parking_services;
static char sent[64];
static struct parking_reply posted;
static char terminal_sent[PAYMENT_FRAME_MAX];

static int remember_send(enum parking_lane_id lane, const uint8_t *data, size_t len)
{
	ARG_UNUSED(lane);
	zassert_true(len < sizeof(sent));
	memcpy(sent, data, len);
	sent[len] = '\0';
	return 0;
}

static int remember_post(const struct parking_reply *reply)
{
	posted = *reply;
	return 0;
}

static int remember_terminal(const uint8_t *data, size_t len)
{
	zassert_true(len < sizeof(terminal_sent));
	memcpy(terminal_sent, data, len);
	terminal_sent[len] = '\0';
	return 0;
}

static int set_tariff(int64_t entered_ms, int64_t now_ms, uint32_t *cents)
{
	zassert_equal(entered_ms, 1);
	zassert_equal(now_ms, 100);
	*cents = 200;
	return 0;
}

static const struct services_exit_io exit_io = {
	.send = fake_terminal_send,
	.show_plate = fake_show_plate,
	.tariff = fake_tariff,
	.show_amount = fake_show_amount,
	.now = fake_now,
	.wake = fake_wake,
};

static void before_each(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(fake_send);
	RESET_FAKE(fake_post);
	RESET_FAKE(fake_wake);
	RESET_FAKE(fake_terminal_send);
	RESET_FAKE(fake_show_plate);
	RESET_FAKE(fake_tariff);
	RESET_FAKE(fake_show_amount);
	RESET_FAKE(fake_now);
	FFF_RESET_HISTORY();
	fake_send_fake.custom_fake = remember_send;
	fake_post_fake.custom_fake = remember_post;
	fake_terminal_send_fake.custom_fake = remember_terminal;
	fake_tariff_fake.custom_fake = set_tariff;
	fake_now_fake.return_val = 100;
	sent[0] = '\0';
	terminal_sent[0] = '\0';
	posted = (struct parking_reply){0};
	services_init(&io);
}

static void camera_says(enum parking_lane_id lane, const char *frame, int64_t now_ms)
{
	for (const char *p = frame; *p != '\0'; p++) {
		services_receive(lane, (uint8_t)*p, now_ms);
	}
	services_process(now_ms);
}

static void plate_frame(char *buf, size_t size, uint32_t id, const char *plate, int conf)
{
	snprintf(buf, size, "+PLATE:%u,\"%s\",%d\r\n", (unsigned int)id, plate, conf);
}

static uint32_t capture(enum parking_lane_id lane, uint32_t transaction, unsigned int attempt,
			int64_t now_ms)
{
	const struct parking_request req = {
		.kind = PARKING_CAPTURE, .lane = lane, .transaction = transaction,
		.attempt = attempt, .deadline_ms = now_ms + 1000,
	};
	unsigned int id;

	zassert_ok(svc->request(&req));
	services_process(now_ms);
	zassert_equal(sscanf(sent, "AT+CAPTURE=%u\r\n", &id), 1, "sent '%s'", sent);
	return id;
}

static void authorize(enum parking_lane_id lane, enum parking_request_kind kind,
		      uint32_t transaction, const char *plate, int64_t now_ms)
{
	struct parking_request req = {
		.kind = kind, .lane = lane, .transaction = transaction,
		.deadline_ms = now_ms + 1000,
	};

	strcpy(req.plate, plate);
	zassert_ok(svc->request(&req));
	services_process(now_ms);
}

static void enter(uint32_t transaction, const char *plate, int64_t now_ms)
{
	authorize(PARKING_ENTRY, PARKING_AUTHORIZE_ENTRY, transaction, plate, now_ms);
	zassert_equal(posted.kind, PARKING_ENTRY_ALLOWED);
	zassert_ok(svc->passage(PARKING_ENTRY, transaction, now_ms + 1));
}

ZTEST(services, test_capture_reports_matching_plate)
{
	char frame[64];
	uint32_t id = capture(PARKING_ENTRY, 3, 1, 0);

	zassert_equal(id, 1);
	zassert_true(fake_wake_fake.call_count >= 1);
	camera_says(PARKING_ENTRY, "OK\r\n", 10);
	zassert_equal(fake_post_fake.call_count, 0, "OK is not a result");
	plate_frame(frame, sizeof(frame), id, "ABC1D23", 93);
	camera_says(PARKING_ENTRY, frame, 110);
	zassert_equal(fake_post_fake.call_count, 1);
	zassert_equal(posted.kind, PARKING_PLATE);
	zassert_equal(posted.lane, PARKING_ENTRY);
	zassert_equal(posted.transaction, 3);
	zassert_equal(posted.attempt, 1);
	zassert_str_equal(posted.plate, "ABC1D23");

	/* A repeated result for the same ID is not reported twice. */
	camera_says(PARKING_ENTRY, frame, 120);
	zassert_equal(fake_post_fake.call_count, 1);
}

ZTEST(services, test_low_confidence_reports_no_plate)
{
	char frame[64];
	uint32_t id = capture(PARKING_ENTRY, 1, 1, 0);

	plate_frame(frame, sizeof(frame), id, "ABC1D23", 84);
	camera_says(PARKING_ENTRY, frame, 100);
	zassert_equal(posted.kind, PARKING_NO_PLATE);
	zassert_equal(posted.attempt, 1);
}

ZTEST(services, test_each_attempt_uses_new_id_and_ignores_stale)
{
	char frame[64];
	uint32_t first = capture(PARKING_ENTRY, 1, 1, 0);
	uint32_t second = capture(PARKING_ENTRY, 1, 2, 1000);

	zassert_not_equal(first, second);
	plate_frame(frame, sizeof(frame), first, "ABC1D23", 93);
	camera_says(PARKING_ENTRY, frame, 1100);
	zassert_equal(fake_post_fake.call_count, 0, "stale ID accepted");
	plate_frame(frame, sizeof(frame), second, "ABC1D23", 93);
	camera_says(PARKING_ENTRY, frame, 1200);
	zassert_equal(posted.attempt, 2);
}

ZTEST(services, test_result_at_deadline_is_ignored)
{
	char frame[64];
	uint32_t id = capture(PARKING_ENTRY, 1, 1, 0);

	plate_frame(frame, sizeof(frame), id, "ABC1D23", 93);
	camera_says(PARKING_ENTRY, frame, 1000);
	zassert_equal(fake_post_fake.call_count, 0);
}

ZTEST(services, test_wrong_lane_result_is_ignored)
{
	char frame[64];
	uint32_t id = capture(PARKING_ENTRY, 1, 1, 0);

	plate_frame(frame, sizeof(frame), id, "ABC1D23", 93);
	camera_says(PARKING_EXIT, frame, 100);
	zassert_equal(fake_post_fake.call_count, 0);
	services_receive(PARKING_LANE_COUNT, '\n', 100);
}

ZTEST(services, test_cancel_invalidates_capture)
{
	char frame[64];
	uint32_t id = capture(PARKING_ENTRY, 1, 1, 0);

	svc->cancel(PARKING_ENTRY, 1);
	plate_frame(frame, sizeof(frame), id, "ABC1D23", 93);
	camera_says(PARKING_ENTRY, frame, 100);
	zassert_equal(fake_post_fake.call_count, 0);
}

ZTEST(services, test_cancelled_request_is_not_sent)
{
	const struct parking_request req = {
		.kind = PARKING_CAPTURE, .lane = PARKING_ENTRY, .transaction = 1,
		.attempt = 1, .deadline_ms = 1000,
	};

	zassert_ok(svc->request(&req));
	svc->cancel(PARKING_ENTRY, 1);
	services_process(0);
	zassert_equal(fake_send_fake.call_count, 0);
}

ZTEST(services, test_entry_reserves_and_passage_creates_session)
{
	enter(1, "ABC1D23", 0);
	zassert_equal(posted.transaction, 1);
	zassert_equal(posted.attempt, 0);

	/* Already parked: denied, even with a new transaction. */
	authorize(PARKING_ENTRY, PARKING_AUTHORIZE_ENTRY, 2, "ABC1D23", 10);
	zassert_equal(posted.kind, PARKING_DENIED);
	zassert_equal(posted.transaction, 2);
}

ZTEST(services, test_full_lot_is_denied)
{
	char plate[PARKING_PLATE_SIZE];
	uint32_t t;

	for (t = 1; t <= CONFIG_PARKING_CAPACITY; t++) {
		snprintf(plate, sizeof(plate), "ABC1D%02u", (unsigned int)t);
		enter(t, plate, t * 10);
	}
	authorize(PARKING_ENTRY, PARKING_AUTHORIZE_ENTRY, t, "XYZ9A00", 1000);
	zassert_equal(posted.kind, PARKING_DENIED);
}

ZTEST(services, test_cancel_releases_reservation)
{
	authorize(PARKING_ENTRY, PARKING_AUTHORIZE_ENTRY, 1, "ABC1D23", 0);
	zassert_equal(posted.kind, PARKING_ENTRY_ALLOWED);
	svc->cancel(PARKING_ENTRY, 1);
	zassert_equal(svc->passage(PARKING_ENTRY, 1, 5), -ESRCH);
	authorize(PARKING_ENTRY, PARKING_AUTHORIZE_ENTRY, 2, "ABC1D23", 10);
	zassert_equal(posted.kind, PARKING_ENTRY_ALLOWED, "spot and plate not released");
}

ZTEST(services, test_passage_rejections)
{
	zassert_equal(svc->passage(PARKING_ENTRY, 0, 0), -EINVAL);
	zassert_equal(svc->passage(PARKING_LANE_COUNT, 1, 0), -EINVAL);
	zassert_equal(svc->passage(PARKING_ENTRY, 9, 0), -ESRCH);

	/* Current transaction without a reservation leaves the table intact. */
	(void)capture(PARKING_ENTRY, 4, 1, 0);
	zassert_equal(svc->passage(PARKING_ENTRY, 4, 10), -ENOENT);

	authorize(PARKING_EXIT, PARKING_AUTHORIZE_EXIT, 5, "ABC1D23", 0);
	zassert_equal(svc->passage(PARKING_EXIT, 5, 10), -EACCES);
}

ZTEST(services, test_exit_never_authorized_without_payment)
{
	enter(1, "ABC1D23", 0);
	authorize(PARKING_EXIT, PARKING_AUTHORIZE_EXIT, 7, "ABC1D23", 100);
	zassert_equal(posted.kind, PARKING_DENIED);
	zassert_equal(posted.lane, PARKING_EXIT);
	authorize(PARKING_EXIT, PARKING_AUTHORIZE_ENTRY, 8, "XYZ9A00", 200);
	zassert_equal(posted.kind, PARKING_DENIED, "entry authorization on exit lane");
}

static void start_exit(uint32_t transaction)
{
	services_exit_init(&exit_io);
	authorize(PARKING_EXIT, PARKING_AUTHORIZE_EXIT, transaction, "ABC1D23", 100);
	services_exit_process();
}

static void terminal_says_paid(void)
{
	char uuid[PAYMENT_UUID_SIZE];
	char result[PAYMENT_FRAME_MAX];

	zassert_equal(sscanf(terminal_sent, "P1 CHARGE %36s", uuid), 1);
	snprintf(result, sizeof(result), "P1 RESULT %s PAID 200 BRL\n", uuid);
	fake_now_fake.return_val = 120;
	for (const char *p = result; *p != '\0'; p++) {
		services_exit_receive((uint8_t)*p, 120);
	}
	services_exit_process();
}

ZTEST(services, test_paid_exit_releases_spot_only_after_passage)
{
	enter(1, "ABC1D23", 0);
	start_exit(7);
	zassert_equal(fake_terminal_send_fake.call_count, 1);
	zassert_equal(fake_show_amount_fake.arg0_val, 200);
	zassert_equal(svc->passage(PARKING_EXIT, 7, 110), -EACCES);

	terminal_says_paid();
	zassert_equal(posted.kind, PARKING_EXIT_PAID);
	zassert_equal(posted.transaction, 7);
	authorize(PARKING_ENTRY, PARKING_AUTHORIZE_ENTRY, 2, "ABC1D23", 130);
	zassert_equal(posted.kind, PARKING_DENIED);
	zassert_ok(svc->passage(PARKING_EXIT, 7, 140));
	zassert_equal(svc->passage(PARKING_EXIT, 7, 141), -ESRCH);
	authorize(PARKING_ENTRY, PARKING_AUTHORIZE_ENTRY, 3, "ABC1D23", 150);
	zassert_equal(posted.kind, PARKING_ENTRY_ALLOWED);
}

ZTEST(services, test_abandoned_payment_cancels_but_keeps_session)
{
	enter(1, "ABC1D23", 0);
	start_exit(7);
	svc->cancel(PARKING_EXIT, 7);
	services_exit_process();
	zassert_equal(fake_terminal_send_fake.call_count, 2);
	zassert_equal(strncmp(terminal_sent, "P1 CANCEL ", 10), 0);
	zassert_equal(svc->passage(PARKING_EXIT, 7, 130), -ESRCH);
	authorize(PARKING_ENTRY, PARKING_AUTHORIZE_ENTRY, 2, "ABC1D23", 140);
	zassert_equal(posted.kind, PARKING_DENIED);
}

ZTEST(services, test_display_failure_does_not_charge)
{
	enter(1, "ABC1D23", 0);
	fake_show_amount_fake.return_val = -EIO;
	start_exit(7);
	zassert_equal(fake_terminal_send_fake.call_count, 0);
	zassert_equal(posted.kind, PARKING_DENIED);
}

ZTEST(services, test_plate_display_failure_skips_tariff_and_charge)
{
	enter(1, "ABC1D23", 0);
	fake_show_plate_fake.return_val = -EIO;
	start_exit(7);
	zassert_equal(fake_tariff_fake.call_count, 0);
	zassert_equal(fake_show_amount_fake.call_count, 0);
	zassert_equal(fake_terminal_send_fake.call_count, 0);
	zassert_equal(posted.kind, PARKING_DENIED);
}

ZTEST(services, test_tariff_overflow_denies_without_transmitting)
{
	enter(1, "ABC1D23", 0);
	fake_tariff_fake.custom_fake = NULL;
	fake_tariff_fake.return_val = -ERANGE;
	start_exit(7);
	zassert_equal(fake_show_plate_fake.call_count, 1);
	zassert_equal(fake_show_amount_fake.call_count, 0);
	zassert_equal(fake_terminal_send_fake.call_count, 0);
	zassert_equal(posted.kind, PARKING_DENIED);
}

ZTEST(services, test_exit_without_session_does_not_render_or_charge)
{
	start_exit(7);
	zassert_equal(fake_show_plate_fake.call_count, 0);
	zassert_equal(fake_terminal_send_fake.call_count, 0);
	zassert_equal(posted.kind, PARKING_DENIED);
}

static int depart_while_drawing(uint32_t cents)
{
	zassert_equal(cents, 200);
	svc->cancel(PARKING_EXIT, 7);
	return 0;
}

ZTEST(services, test_departure_during_spi_prevents_first_charge)
{
	enter(1, "ABC1D23", 0);
	fake_show_amount_fake.custom_fake = depart_while_drawing;
	start_exit(7);
	zassert_equal(fake_show_amount_fake.call_count, 1);
	zassert_equal(fake_terminal_send_fake.call_count, 0);
	zassert_equal(fake_post_fake.call_count, 1, "only the entry was authorized");
}

ZTEST(services, test_expired_queued_exit_does_not_touch_devices)
{
	enter(1, "ABC1D23", 0);
	services_exit_init(&exit_io);
	authorize(PARKING_EXIT, PARKING_AUTHORIZE_EXIT, 7, "ABC1D23", 100);
	fake_now_fake.return_val = 1100;
	services_exit_process();
	zassert_equal(fake_show_plate_fake.call_count, 0);
	zassert_equal(fake_terminal_send_fake.call_count, 0);
	zassert_equal(fake_post_fake.call_count, 1, "only the entry was authorized");
}

ZTEST(services, test_exit_rx_loss_does_not_discard_next_valid_result)
{
	enter(1, "ABC1D23", 0);
	start_exit(7);
	services_exit_receive('P', 105);
	services_exit_receive('1', 105);
	services_exit_resync();
	terminal_says_paid();
	zassert_equal(posted.kind, PARKING_EXIT_PAID);
	zassert_equal(posted.transaction, 7);
}

ZTEST(services, test_reply_queued_before_departure_cannot_authorize)
{
	enter(1, "ABC1D23", 0);
	start_exit(7);
	svc->cancel(PARKING_EXIT, 7);
	terminal_says_paid();
	zassert_equal(fake_terminal_send_fake.call_count, 2);
	zassert_equal(strncmp(terminal_sent, "P1 CANCEL ", 10), 0);
	zassert_equal(fake_post_fake.call_count, 1, "only the entry was authorized");
}

ZTEST(services, test_full_inbox_is_retried_until_deadline)
{
	int results[] = { -ENOMSG, -ENOMSG, 0 };

	fake_post_fake.custom_fake = NULL;
	SET_RETURN_SEQ(fake_post, results, ARRAY_SIZE(results));
	authorize(PARKING_ENTRY, PARKING_AUTHORIZE_ENTRY, 1, "ABC1D23", 0);
	services_process(1);
	services_process(2);
	services_process(3);
	zassert_equal(fake_post_fake.call_count, 3, "posted once delivered");

	/* A reply still undelivered at its deadline is dropped. */
	RESET_FAKE(fake_post);
	fake_post_fake.return_val = -ENOMSG;
	authorize(PARKING_ENTRY, PARKING_AUTHORIZE_ENTRY, 2, "XYZ9A00", 10);
	services_process(1010);
	services_process(1011);
	zassert_equal(fake_post_fake.call_count, 1);
}

ZTEST(services, test_other_post_errors_are_not_retried)
{
	fake_post_fake.custom_fake = NULL;
	fake_post_fake.return_val = -EINVAL;
	authorize(PARKING_ENTRY, PARKING_AUTHORIZE_ENTRY, 1, "ABC1D23", 0);
	services_process(1);
	zassert_equal(fake_post_fake.call_count, 1);
}

ZTEST(services, test_capture_expires_without_result)
{
	char frame[64];
	uint32_t id = capture(PARKING_ENTRY, 1, 1, 0);

	services_process(1000);
	plate_frame(frame, sizeof(frame), id, "ABC1D23", 93);
	camera_says(PARKING_ENTRY, frame, 999);
	zassert_equal(fake_post_fake.call_count, 0, "expired capture revived");
}

ZTEST(services, test_request_validation_and_queue_bound)
{
	struct parking_request req = {
		.kind = PARKING_CAPTURE, .lane = PARKING_ENTRY, .transaction = 1,
		.attempt = 1, .deadline_ms = 1000,
	};
	int rc = 0;
	int accepted = 0;

	zassert_equal(svc->request(NULL), -EINVAL);
	req.transaction = 0;
	zassert_equal(svc->request(&req), -EINVAL);
	req.transaction = 1;
	req.lane = PARKING_LANE_COUNT;
	zassert_equal(svc->request(&req), -EINVAL);
	req.lane = PARKING_ENTRY;
	while (rc == 0 && accepted < 16) {
		rc = svc->request(&req);
		accepted += rc == 0 ? 1 : 0;
	}
	zassert_equal(rc, -ENOMSG);
	zassert_true(accepted > 0);
	svc->cancel(PARKING_LANE_COUNT, 1);
	svc->cancel(PARKING_ENTRY, 0);
}

ZTEST(services, test_send_failure_lets_capture_time_out)
{
	fake_send_fake.custom_fake = NULL;
	fake_send_fake.return_val = -EIO;
	const struct parking_request req = {
		.kind = PARKING_CAPTURE, .lane = PARKING_ENTRY, .transaction = 1,
		.attempt = 1, .deadline_ms = 1000,
	};

	zassert_ok(svc->request(&req));
	services_process(0);
	zassert_equal(fake_send_fake.call_count, 1);
	zassert_equal(fake_post_fake.call_count, 0);
}

ZTEST_SUITE(services, NULL, NULL, before_each, NULL, NULL);
