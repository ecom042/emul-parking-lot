/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Incluido a forca no inicio de hw_adc.c (veja CMakeLists.txt). Os headers
 * reais entram primeiro; os include guards fazem os #include do .c virarem
 * nada, e os #define abaixo mandam as chamadas de driver para os falsos.
 */
#ifndef TEST_HW_ADC_SHIM_H_
#define TEST_HW_ADC_SHIM_H_

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "fakes.h"

#define device_is_ready   fake_device_is_ready
#define adc_channel_setup fake_adc_channel_setup
#define adc_read_async    fake_adc_read_async

#endif /* TEST_HW_ADC_SHIM_H_ */
