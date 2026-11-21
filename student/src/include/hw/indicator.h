#ifndef PARKING_HW_INDICATOR_H_
#define PARKING_HW_INDICATOR_H_

/*
 * Indicator: one on/off output (a light or the buzzer). No parking rules here:
 * the meaning of each indicator lives in services/signals.
 */

#include <stdbool.h>

#include <zephyr/drivers/gpio.h>

/**
 * @brief Configure the pin as an output, initially off.
 *
 * @retval 0 on success.
 * @retval -ENODEV if the GPIO controller is not ready.
 * @retval -errno other error from the GPIO driver.
 */
int indicator_init(const struct gpio_dt_spec *pin);

/**
 * @brief Turn the indicator on or off.
 *
 * @retval 0 on success.
 * @retval -errno error from the GPIO driver.
 */
int indicator_set(const struct gpio_dt_spec *pin, bool on);

#endif /* PARKING_HW_INDICATOR_H_ */
