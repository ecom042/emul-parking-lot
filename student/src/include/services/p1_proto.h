#ifndef PARKING_SERVICES_P1_PROTO_H_
#define PARKING_SERVICES_P1_PROTO_H_

/*
 * Payment terminal wire format (protocol P1): build CHARGE and CANCEL, read
 * one reply line. Syntax only: whether a reply matches the current charge
 * (UUID, amount) is decided by the payment service.
 */

#include <stddef.h>
#include <stdint.h>

#include "services/wire.h"

#define P1_AMOUNT_MAX 1000000u /**< centavos */

enum p1_reply_kind {
	P1_REPLY_INVALID,   /**< not a well-formed reply: ignore it */
	P1_REPLY_PAID,      /**< P1 RESULT <uuid> PAID <amount> BRL */
	P1_REPLY_CANCELLED, /**< P1 RESULT <uuid> CANCELLED <amount> BRL */
	P1_REPLY_ERROR,     /**< P1 ERROR <code> (no correlation ID) */
};

enum p1_error {
	P1_ERR_FRAME,
	P1_ERR_TIMEOUT,
	P1_ERR_CAPACITY,
	P1_ERR_CONFLICT,
	P1_ERR_UNKNOWN,
};

struct p1_reply {
	enum p1_reply_kind kind;
	char uuid[UUID_LEN + 1]; /**< PAID / CANCELLED, NUL-terminated */
	uint32_t amount;         /**< PAID / CANCELLED, centavos */
	enum p1_error error;     /**< ERROR only */
};

/**
 * @brief Format a deterministic UUID for a charge from a counter.
 *
 * Writes "00000000-0000-4000-8000-<seq as 12 lowercase hex digits>" (groups
 * 8-4-4-4-12, NUL-terminated). There is one UUID per charge: the caller
 * increments the counter ONCE, when the charge is created. Retransmissions and
 * CANCEL reuse the string stored with the charge and never call this again.
 * The board has no entropy source, hence the deterministic scheme.
 *
 * @param seq charge counter value
 * @param out fixed buffer of UUID_LEN + 1 bytes
 */
void p1_uuid_format(uint32_t seq, char out[UUID_LEN + 1]);

/**
 * @brief Write "P1 CHARGE <uuid> <plate> <amount> BRL\n" into @p out.
 *
 * @return the length without the NUL, or:
 * @retval -EINVAL a field is malformed (UUID, plate, amount not in 1..P1_AMOUNT_MAX).
 * @retval -ENOSPC @p out is too small.
 */
int p1_proto_format_charge(const char *uuid, const char *plate, uint32_t amount,
			   char *out, size_t size);

/** @brief Write "P1 CANCEL <uuid>\n" into @p out. Same returns as above. */
int p1_proto_format_cancel(const char *uuid, char *out, size_t size);

/** @brief Classify one received line (terminator already removed). */
void p1_proto_parse(const char *line, struct p1_reply *reply);

#endif /* PARKING_SERVICES_P1_PROTO_H_ */
