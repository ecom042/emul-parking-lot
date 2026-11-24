/*
 * Protocolo AT da câmera (REQ-02, REQ-11). Ver camera.h.
 */
#include "camera.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "line_parser.h"

int camera_format_capture(char *out, size_t size, uint32_t id)
{
	int n;

	if (id < CAMERA_ID_MIN || id > CAMERA_ID_MAX) {
		return -EINVAL;
	}
	n = snprintf(out, size, "AT+CAPTURE=%u\r\n", (unsigned int)id);
	/* n negativo (erro) vira um valor enorme no cast e também cai aqui. */
	if ((size_t)n >= size) {
		return -EINVAL;
	}
	return n;
}

static bool is_upper(char c)
{
	return c >= 'A' && c <= 'Z';
}

static bool is_digit(char c)
{
	return c >= '0' && c <= '9';
}

bool camera_plate_valid(const char *plate)
{
	return strlen(plate) == PLATE_LEN &&
	       is_upper(plate[0]) && is_upper(plate[1]) && is_upper(plate[2]) &&
	       is_digit(plate[3]) &&
	       (is_upper(plate[4]) || is_digit(plate[4])) &&
	       is_digit(plate[5]) && is_digit(plate[6]);
}

/* Interpreta o resto de "+PLATE:" : <id>,"<placa>",<confiança> */
static bool parse_plate(const char *s, struct camera_msg *m)
{
	uint32_t id;
	uint32_t confidence;

	if (!lp_parse_uint(&s, CAMERA_ID_MAX, &id) || id < CAMERA_ID_MIN) {
		return false;
	}
	if (s[0] != ',' || s[1] != '"') {
		return false;
	}
	s += 2;
	/* Exatamente 7 caracteres e a aspa de fechamento. */
	if (strlen(s) < PLATE_LEN + 1 || s[PLATE_LEN] != '"') {
		return false;
	}
	memcpy(m->plate, s, PLATE_LEN);
	m->plate[PLATE_LEN] = '\0';
	if (!camera_plate_valid(m->plate)) {
		return false;
	}
	s += PLATE_LEN + 1;
	if (*s != ',') {
		return false;
	}
	s++;
	if (!lp_parse_uint(&s, 100, &confidence) || *s != '\0') {
		return false;
	}
	m->id = id;
	m->confidence = confidence;
	return true;
}

void camera_parse(const char *line, struct camera_msg *m)
{
	static const char plate_prefix[] = "+PLATE:";

	m->kind = CAMERA_INVALID;
	if (strcmp(line, "OK") == 0) {
		m->kind = CAMERA_OK;
	} else if (strcmp(line, "ERROR") == 0) {
		m->kind = CAMERA_ERROR;
	} else if (strncmp(line, plate_prefix, sizeof(plate_prefix) - 1) == 0 &&
		   parse_plate(line + sizeof(plate_prefix) - 1, m)) {
		m->kind = CAMERA_PLATE;
	}
}
