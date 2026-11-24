/**
 * @file camera.c
 * @brief Pedido de captura e leitura do resultado das câmeras.
 */

#include "camera.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PLATE_PREFIX "+PLATE:"

int camera_request(enum hw_uart uart, uint32_t capture_id)
{
	char buf[sizeof("AT+CAPTURE=999999999\r\n")];
	int len;

	if (capture_id == 0U || capture_id > CAMERA_MAX_ID) {
		return -EINVAL;
	}
	len = snprintf(buf, sizeof(buf), "AT+CAPTURE=%" PRIu32 "\r\n", capture_id);
	return hw_uart_write(uart, (const uint8_t *)buf, (size_t)len);
}

/**
 * @brief Lê um decimal canônico de exatamente len caracteres.
 *
 * O caractere em s[len] precisa não ser dígito, para strtoul parar ali.
 */
static int parse_canonical(const char *s, size_t len, unsigned long max, unsigned long *out)
{
	if (len == 0 || strspn(s, "0123456789") != len || (s[0] == '0' && len > 1)) {
		return -EINVAL;
	}
	*out = strtoul(s, NULL, 10);
	return *out <= max ? 0 : -EINVAL;
}

/** @brief Confere o formato [A-Z]{3}[0-9][A-Z0-9][0-9]{2} nos 7 primeiros caracteres. */
static bool plate_valid(const char *s)
{
	const unsigned char *p = (const unsigned char *)s;

	return isupper(p[0]) && isupper(p[1]) && isupper(p[2]) && isdigit(p[3]) &&
	       (isupper(p[4]) || isdigit(p[4])) && isdigit(p[5]) && isdigit(p[6]);
}

int camera_parse_plate(const char *frame, uint32_t capture_id, char *plate,
		       unsigned int *confidence)
{
	const char *p = frame + strlen(PLATE_PREFIX);
	const char *comma;
	unsigned long value;

	if (strncmp(frame, PLATE_PREFIX, strlen(PLATE_PREFIX)) != 0) {
		return -EINVAL;
	}
	comma = strchr(p, ',');
	if (comma == NULL || parse_canonical(p, (size_t)(comma - p), CAMERA_MAX_ID, &value) < 0 ||
	    value != capture_id) {
		return -EINVAL;
	}
	p = comma + 1;
	/* Com a placa válida, p[1..7] não têm NUL, então p[8] e p[9] existem. */
	if (p[0] != '"' || !plate_valid(p + 1) || p[8] != '"' || p[9] != ',') {
		return -EINVAL;
	}
	if (parse_canonical(p + 10, strlen(p + 10), 100, &value) < 0) {
		return -EINVAL;
	}
	memcpy(plate, p + 1, CAMERA_PLATE_LEN);
	plate[CAMERA_PLATE_LEN] = '\0';
	*confidence = (unsigned int)value;
	return 0;
}
