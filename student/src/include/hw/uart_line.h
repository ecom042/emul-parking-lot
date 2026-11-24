#ifndef PARKING_HW_UART_LINE_H_
#define PARKING_HW_UART_LINE_H_

/*
 * UART access without blocking: read whatever bytes have arrived, write a
 * buffer. No framing and no protocol here: services/line_framer and the
 * protocol modules give the bytes meaning.
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

/**
 * @brief Check that the UART is ready.
 *
 * @retval 0 on success.
 * @retval -ENODEV if the device is not ready.
 */
int uart_line_init(const struct device *dev);

/**
 * @brief Copy the bytes already received into @p buf, never waiting.
 *
 * @return the number of bytes copied (0 if none arrived), at most @p size.
 */
int uart_line_read(const struct device *dev, uint8_t *buf, size_t size);

/** @brief Send @p len bytes of @p data, in order. */
void uart_line_write(const struct device *dev, const char *data, size_t len);

#endif /* PARKING_HW_UART_LINE_H_ */
