/*
 * Copyright (C) 2017 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#ifndef __NANDX_PLATFORM_H__
#define __NANDX_PLATFORM_H__

#include "nandx_util.h"

void nandx_platform_enable_clock(struct platform_data *data,
				 bool high_speed_en, bool ecc_clk_en);
void nandx_platform_disable_clock(struct platform_data *data,
				  bool high_speed_en, bool ecc_clk_en);
void nandx_platform_prepare_clock(struct platform_data *data,
				 bool high_speed_en, bool ecc_clk_en);
void nandx_platform_unprepare_clock(struct platform_data *data,
				  bool high_speed_en, bool ecc_clk_en);
int nandx_platform_init(struct platform_data *pdata);
int nandx_platform_power_on(struct platform_data *pdata);
int nandx_platform_power_down(struct platform_data *pdata);

#endif /* __NANDX_PLATFORM_H__ */
