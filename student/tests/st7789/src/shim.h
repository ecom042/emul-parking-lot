/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Incluido a forca antes de st7789.c (veja CMakeLists.txt). Primeiro inclui
 * os headers reais que st7789.c usa; como eles tem include guard, a inclusao
 * dentro de st7789.c nao muda nada. Depois redireciona as chamadas de driver
 * para os falsos do FFF. A logica de st7789.c nao muda.
 */
#ifndef TEST_ST7789_SHIM_H_
#define TEST_ST7789_SHIM_H_

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

#include "fakes.h"

#define spi_is_ready_dt       fake_spi_is_ready_dt
#define spi_write_dt          fake_spi_write_dt
#define gpio_is_ready_dt      fake_gpio_is_ready_dt
#define gpio_pin_configure_dt fake_gpio_pin_configure_dt
#define gpio_pin_set_dt       fake_gpio_pin_set_dt

#endif /* TEST_ST7789_SHIM_H_ */
