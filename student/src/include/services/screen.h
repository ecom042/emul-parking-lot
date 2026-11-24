#ifndef PARKING_SERVICES_SCREEN_H_
#define PARKING_SERVICES_SCREEN_H_

/*
 * The exit lane's payment display. Everything is drawn over the real SPI
 * (hw/st7789); the two charge steps are separate so the state machine can
 * freeze the tariff between them.
 */

#include <stdint.h>

/**
 * @brief Hardware reset and ST7789V start-up (about 400 ms, blocks).
 *
 * Must run in main() before the lane threads start.
 *
 * @retval 0 on success.
 * @retval -errno error from hw/st7789.
 */
int screen_init(void);

/**
 * @brief Charge screen, step 1: the recognized plate.
 *
 * @retval 0 on success.
 * @retval -errno error from hw/st7789.
 */
int screen_show_plate(const char *plate);

/**
 * @brief Charge screen, step 2: "BRL r.cc", "SIMULATED PAYMENT" and "NOT PIX".
 *
 * @retval 0 on success.
 * @retval -errno error from hw/st7789.
 */
int screen_show_amount(uint32_t cents);

#endif /* PARKING_SERVICES_SCREEN_H_ */
