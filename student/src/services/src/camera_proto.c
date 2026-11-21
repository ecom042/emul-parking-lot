#include "services/camera_proto.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define PLATE_PREFIX "+PLATE:"

int camera_proto_format_capture(uint32_t id, char *out, size_t size)
{
	if (id < 1 || id > CAMERA_CAPTURE_ID_MAX) {
		return -EINVAL;
	}
	int n = snprintf(out, size, "AT+CAPTURE=%u\r\n", (unsigned int)id);

	return (n < 0 || (size_t)n >= size) ? -ENOSPC : n;
}

/* Parse <id>,"<plate>",<confidence> (the text after "+PLATE:"). */
static bool parse_plate_fields(const char *s, struct camera_reply *reply)
{
	const char *comma = strchr(s, ',');
	uint32_t confidence;

	if (comma == NULL ||
	    wire_parse_uint(s, (size_t)(comma - s), CAMERA_CAPTURE_ID_MAX,
			    &reply->capture_id) != 0 ||
	    reply->capture_id < 1) {
		return false;
	}

	/* "<plate>" is 9 characters with the quotes, then a comma. */
	const char *quoted = comma + 1;

	if (strlen(quoted) < PLATE_LEN + 3 || quoted[0] != '"' ||
	    quoted[PLATE_LEN + 1] != '"' || quoted[PLATE_LEN + 2] != ',' ||
	    !wire_is_plate(quoted + 1, PLATE_LEN)) {
		return false;
	}

	const char *conf = quoted + PLATE_LEN + 3;

	if (wire_parse_uint(conf, strlen(conf), 100, &confidence) != 0) {
		return false;
	}
	memcpy(reply->plate, quoted + 1, PLATE_LEN);
	reply->plate[PLATE_LEN] = '\0';
	reply->confidence = (uint8_t)confidence;
	return true;
}

void camera_proto_parse(const char *line, struct camera_reply *reply)
{
	reply->kind = CAMERA_REPLY_INVALID;

	if (strcmp(line, "OK") == 0) {
		reply->kind = CAMERA_REPLY_OK;
	} else if (strcmp(line, "ERROR") == 0) {
		reply->kind = CAMERA_REPLY_ERROR;
	} else if (strncmp(line, PLATE_PREFIX, strlen(PLATE_PREFIX)) == 0 &&
		   parse_plate_fields(line + strlen(PLATE_PREFIX), reply)) {
		reply->kind = CAMERA_REPLY_PLATE;
	}
}
