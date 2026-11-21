#include "services/p1_proto.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/sys/util.h>

#define MAX_FIELDS 6
#define LINE_COPY_SIZE 128

static bool is_uuid_str(const char *s)
{
	return wire_is_uuid(s, strlen(s));
}

static int finish(int n, size_t size)
{
	return (n < 0 || (size_t)n >= size) ? -ENOSPC : n;
}

void p1_uuid_format(uint32_t seq, char out[UUID_LEN + 1])
{
	(void)snprintf(out, UUID_LEN + 1, "00000000-0000-4000-8000-%012x",
		       (unsigned int)seq);
}

int p1_proto_format_charge(const char *uuid, const char *plate, uint32_t amount,
			   char *out, size_t size)
{
	if (!is_uuid_str(uuid) || !wire_is_plate(plate, strlen(plate)) ||
	    amount < 1 || amount > P1_AMOUNT_MAX) {
		return -EINVAL;
	}
	return finish(snprintf(out, size, "P1 CHARGE %s %s %u BRL\n", uuid, plate,
			       (unsigned int)amount), size);
}

int p1_proto_format_cancel(const char *uuid, char *out, size_t size)
{
	if (!is_uuid_str(uuid)) {
		return -EINVAL;
	}
	return finish(snprintf(out, size, "P1 CANCEL %s\n", uuid), size);
}

/*
 * Split @p line on single spaces into @p fields (pointers into @p copy).
 * A leading, trailing or doubled space gives an empty field: refused.
 * @return field count, or -1 if malformed or too many fields.
 */
static int split_fields(const char *line, char *copy, size_t copy_size,
			const char *fields[MAX_FIELDS])
{
	int count = 0;

	if (strlen(line) >= copy_size) {
		return -1;
	}
	strcpy(copy, line);
	for (char *p = copy;; ) {
		char *space = strchr(p, ' ');

		if (*p == '\0' || count == MAX_FIELDS) {
			return -1;
		}
		fields[count++] = p;
		if (space == NULL) {
			return count;
		}
		*space = '\0';
		p = space + 1;
	}
}

static bool parse_error(const char *code, struct p1_reply *reply)
{
	static const char *const names[] = {
		[P1_ERR_FRAME] = "FRAME",       [P1_ERR_TIMEOUT] = "TIMEOUT",
		[P1_ERR_CAPACITY] = "CAPACITY", [P1_ERR_CONFLICT] = "CONFLICT",
		[P1_ERR_UNKNOWN] = "UNKNOWN",
	};

	for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
		if (strcmp(code, names[i]) == 0) {
			reply->error = (enum p1_error)i;
			return true;
		}
	}
	return false;
}

/* fields: P1 RESULT <uuid> <PAID|CANCELLED> <amount> BRL */
static bool parse_result(const char *const *f, struct p1_reply *reply)
{
	if (!is_uuid_str(f[2]) || strcmp(f[5], "BRL") != 0 ||
	    wire_parse_uint(f[4], strlen(f[4]), P1_AMOUNT_MAX, &reply->amount) != 0 ||
	    reply->amount < 1) {
		return false;
	}
	if (strcmp(f[3], "PAID") == 0) {
		reply->kind = P1_REPLY_PAID;
	} else if (strcmp(f[3], "CANCELLED") == 0) {
		reply->kind = P1_REPLY_CANCELLED;
	} else {
		return false;
	}
	strcpy(reply->uuid, f[2]);
	return true;
}

void p1_proto_parse(const char *line, struct p1_reply *reply)
{
	char copy[LINE_COPY_SIZE];
	const char *f[MAX_FIELDS];
	int n = split_fields(line, copy, sizeof(copy), f);

	reply->kind = P1_REPLY_INVALID;
	if (n < 2 || strcmp(f[0], "P1") != 0) {
		return;
	}
	if (n == 3 && strcmp(f[1], "ERROR") == 0) {
		if (parse_error(f[2], reply)) {
			reply->kind = P1_REPLY_ERROR;
		}
	} else if (n == 6 && strcmp(f[1], "RESULT") == 0 && !parse_result(f, reply)) {
		reply->kind = P1_REPLY_INVALID;
	}
}
