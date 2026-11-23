/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Incluido a forca antes de hw_io.c (veja CMakeLists.txt). Primeiro inclui os
 * headers reais que hw_io.c usa; como eles tem include guard, a inclusao
 * dentro de hw_io.c nao muda nada. Depois redireciona as chamadas de driver
 * para os falsos do FFF. A logica de hw_io.c nao muda.
 */
#ifndef TEST_HW_IO_SHIM_H_
#define TEST_HW_IO_SHIM_H_

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "fakes.h"

#define gpio_is_ready_dt                fake_gpio_is_ready_dt
#define gpio_pin_configure_dt           fake_gpio_pin_configure_dt
#define gpio_pin_set_dt                 fake_gpio_pin_set_dt
#define gpio_pin_get_dt                 fake_gpio_pin_get_dt
#define gpio_pin_interrupt_configure_dt fake_gpio_pin_interrupt_configure_dt
#define gpio_add_callback_dt            fake_gpio_add_callback_dt

#endif /* TEST_HW_IO_SHIM_H_ */
