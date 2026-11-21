#ifndef PARKING_SERVICES_LINE_FRAMER_H_
#define PARKING_SERVICES_LINE_FRAMER_H_

/*
 * Line framer: turns a UART byte stream into complete text lines.
 *
 * One instance per UART. It never allocates and never blocks: the caller
 * passes each received byte and the current virtual time. A line that is too
 * long, has a forbidden byte, has a wrong terminator or takes too long is
 * discarded through the next LF; a valid-looking prefix is never delivered.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LINE_MAX_BYTES 128      /**< whole frame, terminator included */
#define LINE_ASSEMBLY_MS 1000   /**< deadline to complete a started line */

/** Terminator of the protocol on this UART. */
enum line_mode {
	LINE_CRLF, /**< camera: "\r\n" */
	LINE_LF,   /**< payment terminal: "\n", no CR allowed */
};

enum line_result {
	LINE_NONE,    /**< nothing to report yet */
	LINE_READY,   /**< a complete line is available in line_framer_line() */
	LINE_DROPPED, /**< a bad line was discarded (reported once, at its LF) */
};

struct line_framer {
	enum line_mode mode;
	char buf[LINE_MAX_BYTES];
	size_t len;
	int64_t started_ms;
	bool started;     /**< some byte of the current line was received */
	bool discarding;  /**< skipping bytes through the next LF */
	bool seen_cr;     /**< CRLF mode: the previous byte was a CR */
};

/** @brief Prepare a framer for the given terminator. */
void line_framer_init(struct line_framer *f, enum line_mode mode);

/**
 * @brief Give the framer one received byte.
 *
 * @param now_ms virtual time of the byte.
 * @return LINE_READY when this byte completed a valid line.
 */
enum line_result line_framer_feed(struct line_framer *f, uint8_t byte, int64_t now_ms);

/**
 * @brief Apply the assembly deadline; call it on every poll, even with no bytes.
 *
 * A started line older than LINE_ASSEMBLY_MS is abandoned: the rest of it is
 * discarded through the next LF, which then reports LINE_DROPPED.
 */
void line_framer_check(struct line_framer *f, int64_t now_ms);

/** @brief The last complete line, NUL-terminated, terminator removed. */
const char *line_framer_line(const struct line_framer *f);

#endif /* PARKING_SERVICES_LINE_FRAMER_H_ */
