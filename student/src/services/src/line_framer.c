#include "services/line_framer.h"

#include <stdbool.h>

/* Body room: the terminator takes 2 bytes in CRLF mode, 1 in LF mode. */
static size_t body_max(const struct line_framer *f)
{
	return LINE_MAX_BYTES - (f->mode == LINE_CRLF ? 2 : 1);
}

static void start_over(struct line_framer *f)
{
	f->len = 0;
	f->started = false;
	f->seen_cr = false;
}

/* Bad line: drop what was gathered and skip through the next LF. */
static void begin_discard(struct line_framer *f)
{
	start_over(f);
	f->discarding = true;
}

void line_framer_init(struct line_framer *f, enum line_mode mode)
{
	f->mode = mode;
	f->discarding = false;
	f->buf[0] = '\0';
	start_over(f);
}

void line_framer_check(struct line_framer *f, int64_t now_ms)
{
	if (f->started && now_ms - f->started_ms > LINE_ASSEMBLY_MS) {
		begin_discard(f);
	}
}

/* Handle the LF that ends a line. */
static enum line_result finish_line(struct line_framer *f)
{
	bool crlf_ok = (f->mode == LINE_LF) || f->seen_cr;
	bool empty = (f->len == 0);

	if (!crlf_ok || empty) {
		start_over(f);
		return LINE_DROPPED;
	}
	f->buf[f->len] = '\0';
	f->started = false;
	f->seen_cr = false;
	return LINE_READY;
}

enum line_result line_framer_feed(struct line_framer *f, uint8_t byte, int64_t now_ms)
{
	line_framer_check(f, now_ms);

	if (f->discarding) {
		if (byte == '\n') {
			f->discarding = false;
			return LINE_DROPPED;
		}
		return LINE_NONE;
	}

	if (!f->started) {
		f->started = true;
		f->started_ms = now_ms;
		f->len = 0;
	}

	if (byte == '\n') {
		return finish_line(f);
	}

	/* A CR is only legal right before the LF, and only in CRLF mode. */
	if (f->seen_cr || (byte == '\r' && f->mode == LINE_LF)) {
		begin_discard(f);
		return LINE_NONE;
	}
	if (byte == '\r') {
		f->seen_cr = true;
		return LINE_NONE;
	}

	if (byte < 0x20 || byte > 0x7E || f->len >= body_max(f)) {
		begin_discard(f);
		return LINE_NONE;
	}
	f->buf[f->len++] = (char)byte;
	return LINE_NONE;
}

const char *line_framer_line(const struct line_framer *f)
{
	return f->buf;
}
