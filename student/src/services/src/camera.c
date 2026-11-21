#include "services/camera.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/sys/__assert.h>

#include "hw/uart_line.h"
#include "services/camera_proto.h"
#include "services/events.h"
#include "services/line_framer.h"

#define CAMERA_ATTEMPTS       3
#define CAMERA_ATTEMPT_MS     1000
#define CAMERA_MIN_CONFIDENCE 85
#define CAMERA_READ_CHUNK     32

enum cam_state {
	CAM_IDLE,    /**< no recognition running */
	CAM_SEND,    /**< started: the next poll sends the first capture */
	CAM_WAITING, /**< capture sent: waiting for its +PLATE */
};

/*
 * Per-lane state. camera_start(), camera_abandon() and camera_poll() are
 * called only by the lane's own thread, so there is no mutex.
 */
static struct camera_lane {
	struct line_framer framer;
	enum cam_state state;
	uint8_t attempt;      /**< captures sent in the current recognition */
	uint32_t captures;    /**< captures sent since boot: source of the IDs */
	uint32_t current_id;  /**< ID of the capture being waited for */
	int64_t sent_ms;      /**< virtual time of the current capture */
	bool has_plate;
	char plate[PLATE_LEN + 1];
} cams[LANE_COUNT];

static struct camera_lane *cam_of(enum lane_id id)
{
	__ASSERT(id < LANE_COUNT, "invalid lane %d", id);
	return &cams[id];
}

/* Send a capture with a fresh ID and start its 1000 ms deadline. */
static void send_attempt(enum lane_id id, struct camera_lane *c, int64_t now_ms)
{
	char cmd[32];
	int len;

	c->current_id = (c->captures++ % CAMERA_CAPTURE_ID_MAX) + 1;
	len = camera_proto_format_capture(c->current_id, cmd, sizeof(cmd));
	__ASSERT_NO_MSG(len > 0);
	uart_line_write(lane_hw_get(id)->camera, cmd, (size_t)len);
	c->attempt++;
	c->sent_ms = now_ms;
	c->state = CAM_WAITING;
}

/* The current attempt failed (low confidence or timeout): retry or give up.
 * The state is consistent before the event is published.
 */
static void next_attempt(enum lane_id id, struct camera_lane *c, int64_t now_ms)
{
	if (c->attempt >= CAMERA_ATTEMPTS) {
		c->state = CAM_IDLE;
		(void)events_post(id, EV_CAPTURE_FAILED);
	} else {
		send_attempt(id, c, now_ms);
	}
}

static void handle_line(enum lane_id id, struct camera_lane *c, int64_t now_ms)
{
	struct camera_reply reply;

	camera_proto_parse(line_framer_line(&c->framer), &reply);
	/* OK and ERROR neither restart nor end the deadline; an expired ID or
	 * a reply with no recognition running is ignored.
	 */
	if (c->state != CAM_WAITING || reply.kind != CAMERA_REPLY_PLATE ||
	    reply.capture_id != c->current_id) {
		return;
	}
	if (reply.confidence >= CAMERA_MIN_CONFIDENCE) {
		memcpy(c->plate, reply.plate, sizeof(c->plate));
		c->has_plate = true;
		c->state = CAM_IDLE;
		(void)events_post(id, EV_PLATE_READ);
	} else {
		next_attempt(id, c, now_ms);
	}
}

int camera_init(enum lane_id id)
{
	struct camera_lane *c = cam_of(id);

	memset(c, 0, sizeof(*c));
	line_framer_init(&c->framer, LINE_CRLF);
	return uart_line_init(lane_hw_get(id)->camera);
}

int camera_start(enum lane_id id)
{
	struct camera_lane *c = cam_of(id);

	if (c->state != CAM_IDLE) {
		return -EBUSY;
	}
	c->attempt = 0;
	c->has_plate = false;
	c->state = CAM_SEND;
	return 0;
}

void camera_abandon(enum lane_id id)
{
	cam_of(id)->state = CAM_IDLE;
}

void camera_poll(enum lane_id id, int64_t now_ms)
{
	struct camera_lane *c = cam_of(id);
	uint8_t buf[CAMERA_READ_CHUNK];
	int n = uart_line_read(lane_hw_get(id)->camera, buf, sizeof(buf));

	/* Bytes first: a reply that arrived in time counts even if the
	 * deadline is reached in this same poll.
	 */
	for (int i = 0; i < n; i++) {
		if (line_framer_feed(&c->framer, buf[i], now_ms) == LINE_READY) {
			handle_line(id, c, now_ms);
		}
	}
	line_framer_check(&c->framer, now_ms);

	if (c->state == CAM_SEND) {
		send_attempt(id, c, now_ms);
	} else if (c->state == CAM_WAITING && now_ms - c->sent_ms >= CAMERA_ATTEMPT_MS) {
		next_attempt(id, c, now_ms);
	}
}

int camera_plate(enum lane_id id, char plate[PLATE_LEN + 1])
{
	const struct camera_lane *c = cam_of(id);

	if (!c->has_plate) {
		return -ENOENT;
	}
	memcpy(plate, c->plate, PLATE_LEN + 1);
	return 0;
}
