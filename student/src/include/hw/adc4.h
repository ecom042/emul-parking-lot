#ifndef PARKING_HW_ADC4_H_
#define PARKING_HW_ADC4_H_

/*
 * ADC4, shared by both lanes' distance sensors. One place owns it, one
 * conversion at a time: the request (buffer, sequence, signal) stays owned
 * until the driver reports completion, however late that is, and a new
 * request is refused meanwhile. Nothing here blocks or decides deadlines:
 * the 100 ms limit and the latched fault belong to services/distance.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/adc.h>

#define ADC4_MAX_CODE 4095 /**< 12-bit full scale */

/**
 * @brief Set up the ADC and its single-channel configurations.
 *
 * @param cfgs channel configurations (see ADC_CHANNEL_CFG_DT()).
 * @retval 0 on success.
 * @retval -ENODEV the ADC is not ready.
 * @retval -errno error from adc_channel_setup().
 */
int adc4_init(const struct device *adc, const struct adc_channel_cfg *cfgs, size_t count);

/**
 * @brief Start one asynchronous conversion of @p channel.
 *
 * @retval 0 started.
 * @retval -EBUSY the previous conversion is still owned (not yet completed).
 * @retval -errno error from adc_read_async().
 */
int adc4_start(uint8_t channel);

/**
 * @brief Check the conversion without blocking.
 *
 * Once it returns anything but -EAGAIN the request is released.
 *
 * @retval 0 completed; @p code holds the raw 12-bit sample.
 * @retval -EAGAIN still pending.
 * @retval -EINVAL no conversion was started.
 * @retval -EIO the sample is outside 0..4095.
 * @retval -errno the driver's conversion error.
 */
int adc4_poll(uint16_t *code);

/** @brief True while a conversion is owned (started, not yet collected). */
bool adc4_busy(void);

#endif /* PARKING_HW_ADC4_H_ */
