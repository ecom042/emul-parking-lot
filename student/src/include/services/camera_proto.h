#ifndef PARKING_SERVICES_CAMERA_PROTO_H_
#define PARKING_SERVICES_CAMERA_PROTO_H_

/*
 * Camera wire format (course-defined AT extension): build the capture
 * command and read one reply line. Syntax only. Which capture ID is current,
 * the confidence threshold and the retries belong to the camera service.
 */

#include <stddef.h>
#include <stdint.h>

#include "services/wire.h"

#define CAMERA_CAPTURE_ID_MAX 999999999u

enum camera_reply_kind {
	CAMERA_REPLY_INVALID, /**< not a well-formed reply: ignore it */
	CAMERA_REPLY_OK,      /**< command accepted (not a recognition) */
	CAMERA_REPLY_ERROR,   /**< command rejected */
	CAMERA_REPLY_PLATE,   /**< +PLATE:<id>,"<plate>",<confidence> */
};

struct camera_reply {
	enum camera_reply_kind kind;
	uint32_t capture_id;        /**< PLATE only */
	char plate[PLATE_LEN + 1];  /**< PLATE only, NUL-terminated */
	uint8_t confidence;         /**< PLATE only, 0..100 */
};

/**
 * @brief Write "AT+CAPTURE=<id>\r\n" into @p out (NUL-terminated).
 *
 * @return the length without the NUL, or:
 * @retval -EINVAL @p id is not in 1..CAMERA_CAPTURE_ID_MAX.
 * @retval -ENOSPC @p out is too small.
 */
int camera_proto_format_capture(uint32_t id, char *out, size_t size);

/** @brief Classify one received line (terminator already removed). */
void camera_proto_parse(const char *line, struct camera_reply *reply);

#endif /* PARKING_SERVICES_CAMERA_PROTO_H_ */
