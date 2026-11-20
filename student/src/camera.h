#ifndef PARKING_CAMERA_H
#define PARKING_CAMERA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "parking.h"

/* Frame limit from the device contract, terminator included. */
#define CAMERA_FRAME_MAX 128
#define CAMERA_ID_MAX 999999999U
#define CAMERA_CONFIDENCE_MIN 85U

struct camera_parser {
	char line[CAMERA_FRAME_MAX];
	size_t len;
	bool discard;
};

struct camera_plate {
	uint32_t id;
	unsigned int confidence;
	char plate[PARKING_PLATE_SIZE];
};

/* Plate syntax shared by camera and payment: [A-Z]{3}[0-9][A-Z0-9][0-9]{2}. */
bool camera_plate_valid(const char *plate, size_t len);

void camera_parser_reset(struct camera_parser *parser);

/* Feed one received byte. Returns true only when a complete, well-formed
 * "+PLATE:<id>,\"<plate>\",<confidence>\r\n" frame ends at this byte. OK, ERROR,
 * oversized, non-printable and malformed frames are discarded through LF.
 */
bool camera_parser_feed(struct camera_parser *parser, uint8_t byte, struct camera_plate *out);

/* Writes "AT+CAPTURE=<id>\r\n". Returns its length or -EINVAL. */
int camera_format_capture(uint32_t id, char *buf, size_t size);

#endif
