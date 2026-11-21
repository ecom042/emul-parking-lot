#ifndef PARKING_SERVICES_WIRE_H_
#define PARKING_SERVICES_WIRE_H_

/*
 * Syntax rules shared by the camera and payment protocols: canonical numbers,
 * plates and UUIDs. Pure functions; they only check the text, they know
 * nothing about which capture or charge a value belongs to.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PLATE_LEN 7  /**< "ABC1D23": [A-Z]{3}[0-9][A-Z0-9][0-9]{2} */
#define UUID_LEN 36  /**< lowercase hex, groups 8-4-4-4-12 */

/**
 * @brief Parse a canonical decimal: digits only, no sign, no leading zero.
 *
 * @param s text (not necessarily NUL-terminated).
 * @param len number of characters of @p s.
 * @param max largest accepted value.
 * @param out receives the value on success.
 * @retval 0 on success.
 * @retval -EINVAL empty, not a plain decimal, or has a leading zero.
 * @retval -ERANGE the value is above @p max.
 */
int wire_parse_uint(const char *s, size_t len, uint32_t max, uint32_t *out);

/** @brief True if @p s is exactly a plate: [A-Z]{3}[0-9][A-Z0-9][0-9]{2}. */
bool wire_is_plate(const char *s, size_t len);

/** @brief True if @p s is a lowercase hexadecimal UUID, groups 8-4-4-4-12. */
bool wire_is_uuid(const char *s, size_t len);

#endif /* PARKING_SERVICES_WIRE_H_ */
