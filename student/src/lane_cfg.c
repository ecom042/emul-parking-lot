/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file lane_cfg.c
 * @brief As duas faixas: so a saida tem terminal de pagamento e display.
 */

#include "lane_cfg.h"

const struct lane_cfg lane_cfgs[LANE_COUNT] = {
	[LANE_ENTRY] = {
		.id = LANE_ENTRY,
		.camera = HW_UART_ENTRY_CAMERA,
		.has_payment = false,
	},
	[LANE_EXIT] = {
		.id = LANE_EXIT,
		.camera = HW_UART_EXIT_CAMERA,
		.has_payment = true,
	},
};
