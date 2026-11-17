/*
 * Course coverage builds only: the host pends COURSE_GCOV_IRQ through the NVIC
 * (scripts/parking_embedded.py) at the end of a scenario and before a machine
 * reset, and this handler dumps the gcov counters on the console.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/debug/gcov.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>

/** An interrupt line no device of the parking_lot_u5 board uses. */
#define COURSE_GCOV_IRQ 112

/** Dump the counters gathered so far. */
static void course_gcov_isr(const void *arg)
{
	ARG_UNUSED(arg);
	gcov_coverage_dump();
}

/** Connect and enable the dump interrupt. */
static int course_gcov_init(void)
{
	IRQ_CONNECT(COURSE_GCOV_IRQ, 1, course_gcov_isr, NULL, 0);
	irq_enable(COURSE_GCOV_IRQ);
	return 0;
}

SYS_INIT(course_gcov_init, POST_KERNEL, 0);
