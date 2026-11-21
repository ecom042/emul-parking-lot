#include "hw/uart_line.h"

#include <errno.h>

#include <zephyr/drivers/uart.h>

int uart_line_init(const struct device *dev)
{
	return device_is_ready(dev) ? 0 : -ENODEV;
}

int uart_line_read(const struct device *dev, uint8_t *buf, size_t size)
{
	size_t count = 0;

	while (count < size && uart_poll_in(dev, &buf[count]) == 0) {
		count++;
	}
	return (int)count;
}

void uart_line_write(const struct device *dev, const char *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(dev, (uint8_t)data[i]);
	}
}
