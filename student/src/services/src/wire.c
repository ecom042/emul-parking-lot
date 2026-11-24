#include "services/wire.h"

#include <errno.h>

static bool is_upper(char c)
{
	return c >= 'A' && c <= 'Z';
}

static bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static bool is_lower_hex(char c)
{
	return is_digit(c) || (c >= 'a' && c <= 'f');
}

int wire_parse_uint(const char *s, size_t len, uint32_t max, uint32_t *out)
{
	uint64_t value = 0;

	if (len == 0 || (s[0] == '0' && len > 1)) {
		return -EINVAL;
	}
	for (size_t i = 0; i < len; i++) {
		if (!is_digit(s[i])) {
			return -EINVAL;
		}
		value = value * 10 + (uint64_t)(s[i] - '0');
		if (value > max) {
			return -ERANGE;
		}
	}
	*out = (uint32_t)value;
	return 0;
}

bool wire_is_plate(const char *s, size_t len)
{
	if (len != PLATE_LEN) {
		return false;
	}
	return is_upper(s[0]) && is_upper(s[1]) && is_upper(s[2]) &&
	       is_digit(s[3]) && (is_upper(s[4]) || is_digit(s[4])) &&
	       is_digit(s[5]) && is_digit(s[6]);
}

bool wire_is_uuid(const char *s, size_t len)
{
	if (len != UUID_LEN) {
		return false;
	}
	for (size_t i = 0; i < len; i++) {
		bool dash = (i == 8 || i == 13 || i == 18 || i == 23);

		if (dash ? s[i] != '-' : !is_lower_hex(s[i])) {
			return false;
		}
	}
	return true;
}
