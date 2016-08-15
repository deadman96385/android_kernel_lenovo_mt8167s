/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef _DT_BINDINGS_POWER_MT8167_POWER_H
#define _DT_BINDINGS_POWER_MT8167_POWER_H

#define MT8167_POWER_DOMAIN_MM		0
#define MT8167_POWER_DOMAIN_DISP	0
#define MT8167_POWER_DOMAIN_VDEC	1
#define MT8167_POWER_DOMAIN_ISP		2
#define MT8167_POWER_DOMAIN_MFG_ASYNC	3
#define MT8167_POWER_DOMAIN_MFG_2D	4
#define MT8167_POWER_DOMAIN_MFG		5
#define MT8167_POWER_DOMAIN_CONN	6

#define WORKAROUND_PD_8163	1

#if WORKAROUND_PD_8163

#define INVALID_PD			999
#define MT8167_POWER_DOMAIN_AUDIO	INVALID_PD

#endif /* WORKAROUND_PD_8163 */

#endif /* _DT_BINDINGS_POWER_MT8167_POWER_H */
