#ifndef PARKING_SERVICES_PAYMENT_H_
#define PARKING_SERVICES_PAYMENT_H_

/*
 * Payment terminal (protocol P1), exit lane only. This service knows the
 * protocol and nothing else: it neither calls services/tariff nor
 * services/screen. The state machine freezes the amount and draws the
 * screen, then calls payment_start() with the result.
 */

#include <stdint.h>

/**
 * @brief UART setup for the payment terminal. Before the exit lane thread starts.
 *
 * @retval 0 on success.
 * @retval -ENODEV the terminal UART is not ready.
 */
int payment_init(void);

/**
 * @brief Send P1 CHARGE for an amount the machine already froze.
 *
 * Creates a new UUID, then sends the same frame up to 3 times, 1000 ms each,
 * keeping UUID and amount. Ends with EV_PAID or EV_PAYMENT_FAILED on
 * LANE_EXIT.
 *
 * @param plate recognized plate.
 * @param cents amount in centavos, 1..1000000.
 * @retval 0 on success.
 * @retval -EINVAL invalid plate or amount.
 * @retval -EBUSY a charge is already running.
 */
int payment_start(const char *plate, uint32_t cents);

/** @brief Invalidate the current UUID, then send P1 CANCEL (car left / give up). */
void payment_cancel(void);

/**
 * @brief Read terminal bytes and check deadlines.
 *
 * Called by the exit lane thread on every loop; never blocks.
 *
 * @param now_ms virtual time.
 */
void payment_poll(int64_t now_ms);

#endif /* PARKING_SERVICES_PAYMENT_H_ */
