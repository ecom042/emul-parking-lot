#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "camera.h"

#define PLATE_PREFIX "+PLATE:"

enum { CAMERA_FRAME_TIMEOUT_MS = 1000 };

static bool upper(char c)
{
	return c >= 'A' && c <= 'Z';
}

static bool digit(char c)
{
	return c >= '0' && c <= '9';
}

bool camera_plate_valid(const char *plate, size_t len)
{
	return len == PARKING_PLATE_SIZE - 1 && upper(plate[0]) && upper(plate[1]) &&
	       upper(plate[2]) && digit(plate[3]) && (upper(plate[4]) || digit(plate[4])) &&
	       digit(plate[5]) && digit(plate[6]);
}

/* Canonical decimal: at least one digit, no leading zero, at most max. */
static bool parse_number(const char **cursor, const char *end, uint32_t max, uint32_t *out)
{
	const char *p = *cursor;
	uint64_t value = 0;

	if (p == end || !digit(*p) || (*p == '0' && p + 1 != end && digit(p[1]))) {
		return false;
	}
	while (p != end && digit(*p)) {
		value = value * 10U + (uint64_t)(*p - '0');
		if (value > max) {
			return false;
		}
		p++;
	}
	*cursor = p;
	*out = (uint32_t)value;
	return true;
}

static bool expect(const char **cursor, const char *end, char c)
{
	if (*cursor == end || **cursor != c) {
		return false;
	}
	(*cursor)++;
	return true;
}

static bool parse_plate_frame(const char *line, size_t len, struct camera_plate *out)
{
	const size_t prefix = sizeof(PLATE_PREFIX) - 1;
	const char *end = line + len;
	const char *p = line + prefix;
	const char *plate;
	uint32_t id;
	uint32_t confidence;

	if (len <= prefix || memcmp(line, PLATE_PREFIX, prefix) != 0 ||
	    !parse_number(&p, end, CAMERA_ID_MAX, &id) || id == 0 || !expect(&p, end, ',') ||
	    !expect(&p, end, '"')) {
		return false;
	}
	plate = p;
	while (p != end && *p != '"') {
		p++;
	}
	if (!camera_plate_valid(plate, (size_t)(p - plate)) || !expect(&p, end, '"') ||
	    !expect(&p, end, ',') || !parse_number(&p, end, 100U, &confidence) || p != end) {
		return false;
	}
	out->id = id;
	out->confidence = confidence;
	memcpy(out->plate, plate, PARKING_PLATE_SIZE - 1);
	out->plate[PARKING_PLATE_SIZE - 1] = '\0';
	return true;
}

void camera_parser_reset(struct camera_parser *parser)
{
	parser->len = 0;
	parser->discard = false;
}

bool camera_parser_feed(struct camera_parser *parser, uint8_t byte, int64_t received_ms,
			struct camera_plate *out)
{
	if (parser->len > 0 && received_ms - parser->started_ms >= CAMERA_FRAME_TIMEOUT_MS) {
		parser->discard = true;
	}
	if (byte == '\n') {
		bool complete = !parser->discard && parser->len > 0 &&
				parser->line[parser->len - 1] == '\r';
		size_t len = parser->len;

		camera_parser_reset(parser);
		return complete && parse_plate_frame(parser->line, len - 1, out);
	}
	if (parser->discard) {
		return false;
	}
	/* CR is only valid right before LF; 127 retained bytes plus LF is the limit. */
	bool after_cr = parser->len > 0 && parser->line[parser->len - 1] == '\r';
	bool printable = byte >= 0x20 && byte <= 0x7e;

	if (after_cr || (!printable && byte != '\r') || parser->len == CAMERA_FRAME_MAX - 1) {
		parser->discard = true;
		return false;
	}
	if (parser->len == 0) {
		parser->started_ms = received_ms;
	}
	parser->line[parser->len++] = (char)byte;
	return false;
}

int camera_format_capture(uint32_t id, char *buf, size_t size)
{
	if (id == 0 || id > CAMERA_ID_MAX) {
		return -EINVAL;
	}
	int len = snprintf(buf, size, "AT+CAPTURE=%u\r\n", (unsigned int)id);

	return len > 0 && (size_t)len < size ? len : -EINVAL;
}
