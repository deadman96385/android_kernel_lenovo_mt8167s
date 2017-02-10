/*
 * Copyright (C) 2016 MediaTek Inc.
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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <mt-plat/aee.h>
#include <linux/i2c.h>
#include <linux/of_fdt.h>
#include <asm/setup.h>
#include <linux/lockdep.h>
#include <mt-plat/mtk_cirq.h>
#include "mtk_spm_sleep.h"
#include "mtk_cpuidle.h"
/* #include <mach/wd_api.h> */

#include "mtk_spm_internal.h"

/* for MP0,1 AXI_CONFIG */
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>

/**************************************
 * only for internal debug
 **************************************/
#ifdef CONFIG_MTK_LDVT
#define SPM_PWAKE_EN            0
#define SPM_PCMWDT_EN           0
#define SPM_BYPASS_SYSPWREQ     1
#else
#define SPM_PWAKE_EN            1
#define SPM_PCMWDT_EN           0
#define SPM_BYPASS_SYSPWREQ     0
#endif
#define SPM_PCMTIMER_DIS        0


#define I2C_CHANNEL 2

int spm_dormant_sta;	/* MT_CPU_DORMANT_RESET */
u32 spm_suspend_flag;

struct wake_status suspend_info[20];
u32 log_wakesta_cnt;
u32 log_wakesta_index;
u8 spm_snapshot_golden_setting;

struct wake_status spm_wakesta;	/* record last wakesta */

/**********************************************************
 * PCM code for suspend
 **********************************************************/
static const u32 suspend_binary[] = {
	0x803d0400, 0x803b0400, 0xa1d58407, 0x81f68407, 0x803a0400, 0x1b80001f,
	0x20000000, 0x80300400, 0x80328400, 0xa1d28407, 0x81f20407, 0x80338400,
	0x80318400, 0xa15f8405, 0x82851801, 0xd80002ca, 0x17c07c1f, 0x18c0001f,
	0x10006234, 0xc0c023c0, 0x1200041f, 0xa15e8405, 0x81f00407, 0xc2803160,
	0x1290041f, 0x82069801, 0xd8000828, 0x17c07c1f, 0xe8208000, 0x10210004,
	0x00001d00, 0xe8208000, 0x10210008, 0x00912345, 0xe8208000, 0x10210000,
	0x00080401, 0x1b80001f, 0x20000032, 0x1a10001f, 0x10210000, 0xe8208000,
	0x10210c40, 0x00009f8b, 0xe8208000, 0x10210c44, 0x00000902, 0xe8208000,
	0x10210c48, 0x00000109, 0xe8208000, 0x10210c4c, 0x00000000, 0xe8208000,
	0x10210c40, 0x00089f8b, 0xe8208000, 0x10210004, 0x00001d00, 0x1b80001f,
	0x200000c8, 0x1a10001f, 0x10210c40, 0x1a10001f, 0x10210c50, 0x1b00001f,
	0x7fffd7ff, 0xe8208000, 0x10000054, 0x00400000, 0xf0000000, 0x17c07c1f,
	0x1b00001f, 0x3fffc7ff, 0xd8000dac, 0x17c07c1f, 0xc0c02960, 0x17c07c1f,
	0xe8208000, 0x10000084, 0x00400000, 0x82851801, 0xd8000b2a, 0x17c07c1f,
	0x18c0001f, 0x10006234, 0xc0c02540, 0x17c07c1f, 0xa15e0405, 0xc2803160,
	0x1290841f, 0xa0138400, 0x1b00001f, 0x3fffcfff, 0xa0128400, 0xa0118400,
	0x81f58407, 0xa1d68407, 0x82069801, 0x82471801, 0x82a02401, 0xa15f0405,
	0xa1d20407, 0x81f28407, 0xa0100400, 0xd8000da8, 0xa01a0400, 0xa01d2400,
	0xa01b2800, 0xf0000000, 0x17c07c1f, 0x82059801, 0xd8000e88, 0xa15f8405,
	0x80318400, 0x80328400, 0x1b00001f, 0x7fffd7ff, 0xf0000000, 0x17c07c1f,
	0x82059801, 0xd8001028, 0xa15f0405, 0xa0128400, 0xa0118400, 0xc2803160,
	0x1290841f, 0xc2803160, 0x129f041f, 0x1b00001f, 0x3fffcfff, 0xf0000000,
	0x17c07c1f, 0x81f30407, 0x82831801, 0xd80017ca, 0x17c07c1f, 0x82839801,
	0xd800138a, 0x17c07c1f, 0x1a00001f, 0x10006240, 0xe2200016, 0xe220001e,
	0x1b80001f, 0x2000001a, 0xe220000e, 0xe220000f, 0xe220000d, 0x1a00001f,
	0x10006c60, 0x1a50001f, 0x10006c60, 0xa2540409, 0xe2000009, 0xa15d8405,
	0x1b80001f, 0x20000300, 0xa2548409, 0xe2000009, 0x81fa8407, 0x81f80407,
	0x1b80001f, 0x20000004, 0x81f88407, 0x1b80001f, 0x20000004, 0x80370400,
	0x80310400, 0x1b80001f, 0x20000004, 0x80368400, 0xe8208000, 0x102111a4,
	0x0ffffffc, 0xe8208000, 0x102121a4, 0x0ffffffc, 0xe8208000, 0x102112a0,
	0xc100008c, 0xe8208000, 0x102122a0, 0xc100008c, 0x81fa0407, 0x81f08407,
	0x821a840d, 0xd8001748, 0x17c07c1f, 0xa15c8405, 0xe8208000, 0x10006354,
	0xfffff423, 0xa1df0407, 0xc2803160, 0x1291041f, 0x1b00001f, 0xbfffc7ff,
	0xf0000000, 0x17c07c1f, 0x1b80001f, 0x20000a50, 0x1a50001f, 0x10006608,
	0x820ba401, 0x8217a00d, 0xd8201b88, 0x17c07c1f, 0x81f08407, 0xa1df0407,
	0x1b00001f, 0x3fffc7ff, 0x1b80001f, 0x20000004, 0xd800238c, 0x17c07c1f,
	0x1b00001f, 0xbfffc7ff, 0xd800238c, 0x17c07c1f, 0x81ff0407, 0x82831801,
	0xd800228a, 0x17c07c1f, 0xc0c02800, 0x17c07c1f, 0xd8001a03, 0x17c07c1f,
	0xa1da0407, 0xa0170400, 0xe8208000, 0x102112a0, 0xc1000080, 0xe8208000,
	0x102122a0, 0xc1000080, 0xe8208000, 0x102111a4, 0x0ffffff8, 0xe8208000,
	0x102121a4, 0x0ffffff8, 0xa0168400, 0xa0110400, 0xa1d88407, 0xa1d80407,
	0xa1da8407, 0x1a00001f, 0x10006c60, 0x1a700008, 0x17c07c1f, 0x82748409,
	0xe2000009, 0xa15c0405, 0x1ad0001f, 0x10006b00, 0x82d1840b, 0xd8001fcb,
	0x17c07c1f, 0x82839801, 0xd800228a, 0x17c07c1f, 0x1a00001f, 0x10006c60,
	0x82740409, 0xe2000009, 0x18c0001f, 0x10006240, 0xc0c02760, 0x17c07c1f,
	0xa15d0405, 0x1ad0001f, 0x10006b00, 0x82d2040b, 0xd80021eb, 0x17c07c1f,
	0x82041801, 0xd8002308, 0x17c07c1f, 0xa1d30407, 0xc2803160, 0x1291841f,
	0x1b00001f, 0x7fffd7ff, 0xf0000000, 0x17c07c1f, 0xe0f07f16, 0x1380201f,
	0xe0f07f1e, 0x1380201f, 0xe0f07f0e, 0x1b80001f, 0x20000100, 0xe0f07f0c,
	0xe0f07f0d, 0xe0f07e0d, 0xf0000000, 0x17c07c1f, 0xe0f07f0d, 0xe0f07f0f,
	0xe0f07f1e, 0xe0f07f12, 0x1b80001f, 0x2000000a, 0xf0000000, 0x17c07c1f,
	0xe0e00016, 0x1380201f, 0xe0e0001e, 0x1380201f, 0xe0e0000e, 0xe0e0000c,
	0xe0e0000d, 0xf0000000, 0x17c07c1f, 0xe0e0000f, 0xe0e0001e, 0xe0e00012,
	0xf0000000, 0x17c07c1f, 0x1212841f, 0xa1d08407, 0xd82028c8, 0x80eab401,
	0xd8002843, 0x02200408, 0x1a00001f, 0x10006814, 0xe2000003, 0xf0000000,
	0x17c07c1f, 0xa1d00407, 0x1b80001f, 0x20000100, 0x80ea3401, 0x1a00001f,
	0x10006814, 0xe2000003, 0xf0000000, 0x17c07c1f, 0xd8002b4a, 0x17c07c1f,
	0xe2e00036, 0xe2e0003e, 0x1380201f, 0xe2e0003c, 0xd8202c8a, 0x17c07c1f,
	0x1b80001f, 0x20000018, 0xe2e0007c, 0x1b80001f, 0x20000003, 0xe2e0005c,
	0xe2e0004c, 0xe2e0004d, 0xf0000000, 0x17c07c1f, 0xa1d40407, 0x1391841f,
	0xf0000000, 0x17c07c1f, 0xd8002dea, 0x17c07c1f, 0xe2e0004f, 0xe2e0006f,
	0xe2e0002f, 0xd8202e8a, 0x17c07c1f, 0xe2e0002e, 0xe2e0003e, 0xe2e00032,
	0xf0000000, 0x17c07c1f, 0x82481801, 0xd8202fa9, 0x17c07c1f, 0x1b80001f,
	0x20000050, 0xd8003049, 0x17c07c1f, 0x18d0001f, 0x10006604, 0x10cf8c1f,
	0xd8202ec3, 0x17c07c1f, 0xf0000000, 0x17c07c1f, 0x1ad0001f, 0x10006b00,
	0x82c0280b, 0xd800308b, 0x17c07c1f, 0xf0000000, 0x17c07c1f, 0x1a00001f,
	0x10006b18, 0x1a50001f, 0x10006b18, 0xa2402809, 0xe2000009, 0xf0000000,
	0x17c07c1f, 0xe2200000, 0x1a700008, 0x17c07c1f, 0x17c07c1f, 0x82482401,
	0xd8003289, 0x17c07c1f, 0xf0000000, 0x17c07c1f, 0xe2200004, 0x1a700008,
	0x17c07c1f, 0x17c07c1f, 0x82482401, 0xd82033a9, 0x17c07c1f, 0xf0000000,
	0x17c07c1f, 0xe220004f, 0xe220000f, 0xe220002f, 0xe220002e, 0xe220003e,
	0xe2200032, 0xf0000000, 0x17c07c1f, 0xe2200036, 0x1a50001f, 0x1000660c,
	0x82400809, 0xd82035c9, 0x17c07c1f, 0xe220003e, 0x1a50001f, 0x10006610,
	0x82400809, 0xd8203689, 0x17c07c1f, 0xe220007e, 0xe220007c, 0xe220005c,
	0xe220004c, 0xe220004d, 0xf0000000, 0x17c07c1f, 0xc7c034a0, 0x17c07c1f,
	0xe2400001, 0x1259081f, 0x1a10001f, 0x1000660c, 0x82002408, 0xd8203888,
	0x17c07c1f, 0x1a10001f, 0x10006c60, 0xa2002808, 0x1a40001f, 0x10006c60,
	0xe2400008, 0xf0000000, 0x17c07c1f, 0xc7c034a0, 0x17c07c1f, 0xd8003bea,
	0x1254041f, 0xe8208000, 0x10006248, 0x00000000, 0x1a10001f, 0x10006248,
	0x82002408, 0xd8003b08, 0x17c07c1f, 0xf0000000, 0x17c07c1f, 0xe8208000,
	0x10006244, 0x00000001, 0x1a10001f, 0x10006244, 0x82002408, 0xd8203c48,
	0x17c07c1f, 0xf0000000, 0x17c07c1f, 0x18d0001f, 0x10006c60, 0x82800c0a,
	0x18c0001f, 0x10006c60, 0xe0c0000a, 0xe240001f, 0x1259081f, 0x1a90001f,
	0x1000660c, 0x82802809, 0xd8003e2a, 0x17c07c1f, 0xc7c035a0, 0x17c07c1f,
	0xf0000000, 0x17c07c1f, 0xd80040ca, 0x1254041f, 0xe8208000, 0x10006248,
	0x00000001, 0x1a90001f, 0x10006248, 0x8280240a, 0xd8203fea, 0x17c07c1f,
	0xd00041c0, 0x17c07c1f, 0xe8208000, 0x10006244, 0x00000000, 0x1a90001f,
	0x10006244, 0x8280240a, 0xd800412a, 0x17c07c1f, 0xc7c035a0, 0x17c07c1f,
	0xf0000000, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
	0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x1840001f, 0x00000001,
	0xa1d48407, 0x1990001f, 0x10006b08, 0xe8208000, 0x10006b18, 0x00000000,
	0x11407c1f, 0x1b00001f, 0x3fffc7ff, 0x1b80001f, 0xd00f0000, 0xd8009bac,
	0x17c07c1f, 0xa1578405, 0xe8208000, 0x10006354, 0xfffff423, 0xa1d10407,
	0x1b80001f, 0x20000020, 0x82801801, 0xd80087ca, 0x17c07c1f, 0x81f60407,
	0x1a00001f, 0x10006200, 0x1a40001f, 0x1000625c, 0x1096841f, 0xc2c03800,
	0x1290041f, 0xa1580405, 0xc2003080, 0x1290041f, 0x82809801, 0xd80087ca,
	0x17c07c1f, 0x1a00001f, 0x10006208, 0xc2c03a20, 0x82811801, 0xa1590405,
	0xc2003080, 0x1290841f, 0x82819801, 0xd80087ca, 0x17c07c1f, 0x1a00001f,
	0x10006290, 0xc20034a0, 0x17c07c1f, 0x1b80001f, 0x20000027, 0x82811801,
	0xa1d1a807, 0xa15a0405, 0xc2003080, 0x1291041f, 0xc2803160, 0x1292041f,
	0x1a00001f, 0x10006294, 0xe8208000, 0x10006294, 0x0003ffff, 0xe2203fff,
	0xe22003ff, 0x82021801, 0xd8008a68, 0x17c07c1f, 0x1a10001f, 0x10000004,
	0x1a40001f, 0x10000004, 0x82378408, 0xe2400008, 0x1b80001f, 0x2000000a,
	0xa1d40407, 0x1b80001f, 0x20000003, 0x82049801, 0xd8008b88, 0x17c07c1f,
	0xe8208000, 0x10006604, 0x00000001, 0xc0c02ec0, 0x17c07c1f, 0xa15b0405,
	0x82879801, 0xd8008dea, 0x17c07c1f, 0xa1d38407, 0x82829801, 0xd8008c8a,
	0x17c07c1f, 0xa1d98407, 0xa0108400, 0xa0120400, 0xa0148400, 0xa0188400,
	0xd8008dea, 0x17c07c1f, 0xa0150400, 0xa0158400, 0xa01f0400, 0xa0190400,
	0xa0198400, 0xe8208000, 0x10006310, 0x0b160008, 0xe8208000, 0x10006c00,
	0x0000000f, 0x1b00001f, 0xbfffc7ff, 0x82861801, 0xb2859941, 0xd820904a,
	0x17c07c1f, 0xe8208000, 0x10006c00, 0x0000003c, 0x1b80001f, 0x90100000,
	0xd0009120, 0x17c07c1f, 0x1b80001f, 0x90100000, 0x82000001, 0xc8c00008,
	0x17c07c1f, 0xd0009180, 0x17c07c1f, 0x82028001, 0xc8c00de8, 0x17c07c1f,
	0x820f1c01, 0xc8e010a8, 0x17c07c1f, 0x1b00001f, 0x3fffc7ff, 0x1a00001f,
	0x10006294, 0xe22007fe, 0xe2200ffc, 0xe2201ff8, 0xe2203ff0, 0xe2203fe0,
	0xe2203fc0, 0x1b80001f, 0x20000020, 0xe8208000, 0x10006294, 0x0003ffc0,
	0xe8208000, 0x10006294, 0x0003fc00, 0x82049801, 0xd8009548, 0x17c07c1f,
	0xe8208000, 0x10006604, 0x00000000, 0xc0c02ec0, 0x17c07c1f, 0xa15b8405,
	0x80388400, 0x80390400, 0x80398400, 0x803f0400, 0x1b80001f, 0x20000300,
	0x80348400, 0x80350400, 0x80358400, 0x1b80001f, 0x20000104, 0x10007c1f,
	0x81f38407, 0x81f98407, 0x81f40407, 0x82801801, 0xd8009baa, 0x17c07c1f,
	0x82809801, 0xd80099ea, 0x17c07c1f, 0x82819801, 0xd800992a, 0x17c07c1f,
	0x82811801, 0x81f1a807, 0x1a00001f, 0x10006290, 0xc20035a0, 0x1097841f,
	0xa15a8405, 0x1a00001f, 0x10006208, 0x1094841f, 0xc2c03f40, 0x82811801,
	0xa1598405, 0x1a00001f, 0x10006200, 0x1a40001f, 0x1000625c, 0x1096841f,
	0xc2c03d20, 0xa2b0041f, 0xa1588405, 0x1a10001f, 0x10000004, 0x1a40001f,
	0x10000004, 0xa2178408, 0xe2400008, 0x82079401, 0xd8009ea8, 0x17c07c1f,
	0x82001801, 0xd8009ea8, 0x17c07c1f, 0xa1570405, 0x13007c1f, 0x1b80001f,
	0xd00f0000, 0x1a00001f, 0x10006200, 0x1a40001f, 0x1000625c, 0x1096841f,
	0xc2c03800, 0x1290041f, 0x1a00001f, 0x10006200, 0x1a40001f, 0x1000625c,
	0x1096841f, 0xc2c03d20, 0xa2b0041f, 0xa1db0407, 0x81f10407, 0xa1d60407,
	0x81f48407, 0x1ac0001f, 0x55aa55aa, 0xf0000000
};

static struct pcm_desc suspend_pcm = {
	.version	= "SUSPEND_V0.4_8_6_1",
	.base		= suspend_binary,
	.size		= 1276,
	.sess		= 2,
	.replace	= 0,
	.addr_2nd	= 0,
	.vec0		= EVENT_VEC(11, 1, 0, 0),	/* FUNC_26M_WAKEUP */
	.vec1		= EVENT_VEC(12, 1, 0, 72),	/* FUNC_26M_SLEEP */
	.vec2		= EVENT_VEC(30, 1, 0, 133),	/* FUNC_APSRC_WAKEUP */
	.vec3		= EVENT_VEC(31, 1, 0, 200),	/* FUNC_APSRC_SLEEP */
	.vec4		= EVENT_VEC(11, 1, 0, 111),	/* FUNC_26MIDLE_WAKEUP */
	.vec5		= EVENT_VEC(12, 1, 0, 120),	/* FUNC_26MIDLE_SLEEP */
};

/**************************************
 * SW code for suspend
 **************************************/
#define SPM_SYSCLK_SETTLE       128      /* 3.9ms */

#define WAIT_UART_ACK_TIMES     10	/* 10 * 10us */

#define SPM_WAKE_PERIOD         600	/* sec */

#define WAKE_SRC_FOR_SUSPEND \
	(WAKE_SRC_KP | WAKE_SRC_EINT |  WAKE_SRC_CONN_WDT  | WAKE_SRC_CONN2AP | \
	WAKE_SRC_USB_CD | WAKE_SRC_USB_PDN |  WAKE_SRC_SEJ | \
	WAKE_SRC_SYSPWREQ)

#define spm_is_wakesrc_invalid(wakesrc)     (!!((u32)(wakesrc) & 0xFC7F3A9B))

#define reg_read(addr)         __raw_readl((void __force __iomem *)(addr))
#define reg_write(addr, val)   mt_reg_sync_writel((val), ((void *)addr))

#ifdef CONFIG_MTK_RAM_CONSOLE
#define SPM_AEE_RR_REC 1
#else
#define SPM_AEE_RR_REC 0
#endif

#if SPM_AEE_RR_REC
enum spm_suspend_step {
	SPM_SUSPEND_ENTER = 0,
	SPM_SUSPEND_ENTER_WFI,
	SPM_SUSPEND_LEAVE_WFI,
	SPM_SUSPEND_LEAVE
};
/* extern void aee_rr_rec_spm_suspend_val(u32 val); */
/* extern u32 aee_rr_curr_spm_suspend_val(void); */
void __attribute__ ((weak)) aee_rr_rec_spm_suspend_val(u32 val)
{
}

u32 __attribute__ ((weak)) aee_rr_curr_spm_suspend_val(void)
{
	return 0;
}
#endif

int __attribute__ ((weak)) get_dynamic_period(int first_use, int first_wakeup_time,
					      int battery_capacity_level)
{
	return 0;
}

void __attribute__ ((weak)) mt_cirq_clone_gic(void)
{
}

void __attribute__ ((weak)) mt_cirq_enable(void)
{
}

void __attribute__ ((weak)) mt_cirq_flush(void)
{
}

void __attribute__ ((weak)) mt_cirq_disable(void)
{
}

int __attribute__ ((weak)) request_uart_to_sleep(void)
{
	return 0;
}

int __attribute__ ((weak)) request_uart_to_wakeup(void)
{
	return 0;
}

void __attribute__ ((weak)) mtk_uart_restore(void)
{
}

void __attribute__ ((weak)) mt_eint_print_status(void)
{
}

static struct pwr_ctrl suspend_ctrl = {
	.wake_src = WAKE_SRC_FOR_SUSPEND,
	.r0_ctrl_en = 1,
	.r7_ctrl_en = 1,
	.infra_dcm_lock = 1,
	.wfi_op = WFI_OP_AND,

	.ca7top_idle_mask = 0,
	.ca15top_idle_mask = 1,
	.mcusys_idle_mask = 0,
	.disp0_req_mask = 0,
	.disp1_req_mask = 0,
	.mfg_req_mask = 0,
	.vdec_req_mask = 0,
	.mm_ddr_req_mask = 0,
	.conn_mask = 0,
#ifdef CONFIG_MTK_NFC
	.srclkenai_mask = 0,	/* unmask for NFC use */
#else
	.srclkenai_mask = 1,	/* mask for gpio/i2c use */
#endif
	/* .pcm_apsrc_req = 1, */

	/* .pcm_f26m_req = 1, */

	.ca7_wfi0_en = 1,
	.ca7_wfi1_en = 1,
	.ca7_wfi2_en = 1,
	.ca7_wfi3_en = 1,
	.ca15_wfi0_en = 0,
	.ca15_wfi1_en = 0,
	.ca15_wfi2_en = 0,
	.ca15_wfi3_en = 0,

#if SPM_BYPASS_SYSPWREQ
	.syspwreq_mask = 1,
#endif
};

struct spm_lp_scen __spm_suspend = {
	.pcmdesc = &suspend_pcm,
	.pwrctrl = &suspend_ctrl,
	.wakestatus = &suspend_info[0],
};

static void spm_set_sysclk_settle(void)
{
	u32 settle;

	/* SYSCLK settle = VTCXO settle time */
	spm_write(SPM_CLK_SETTLE, SPM_SYSCLK_SETTLE);
	settle = spm_read(SPM_CLK_SETTLE);

	spm_warn("settle = %u\n", settle);
}

static void spm_kick_pcm_to_run(struct pwr_ctrl *pwrctrl)
{
	/* enable PCM WDT (normal mode) to start count if needed */
#if SPM_PCMWDT_EN
	{
		u32 con1;

		con1 = spm_read(SPM_PCM_CON1) & ~(CON1_PCM_WDT_WAKE_MODE | CON1_PCM_WDT_EN);
		spm_write(SPM_PCM_CON1, CON1_CFG_KEY | con1);

		if (spm_read(SPM_PCM_TIMER_VAL) > PCM_TIMER_MAX)
			spm_write(SPM_PCM_TIMER_VAL, PCM_TIMER_MAX);
		spm_write(SPM_PCM_WDT_TIMER_VAL, spm_read(SPM_PCM_TIMER_VAL) + PCM_WDT_TIMEOUT);
		spm_write(SPM_PCM_CON1, con1 | CON1_CFG_KEY | CON1_PCM_WDT_EN);
	}
#endif

#if SPM_PCMTIMER_DIS
	{
		u32 con1;

		con1 = spm_read(SPM_PCM_CON1) & ~(CON1_PCM_TIMER_EN);
		spm_write(SPM_PCM_CON1, con1 | CON1_CFG_KEY);
	}
#endif

	/* init PCM_PASR_DPD_0 for DPD */
	spm_write(SPM_PCM_PASR_DPD_0, 0);

	__spm_kick_pcm_to_run(pwrctrl);
}

static void spm_trigger_wfi_for_sleep(struct pwr_ctrl *pwrctrl)
{
	if (is_cpu_pdn(pwrctrl->pcm_flags)) {
		spm_dormant_sta = mt_cpu_dormant(CPU_SHUTDOWN_MODE /* | DORMANT_SKIP_WFI */);
		switch (spm_dormant_sta) {
		case MT_CPU_DORMANT_RESET:
			break;
		case MT_CPU_DORMANT_BYPASS:
			break;
		}
	} else {
		spm_dormant_sta = -1;
		wfi_with_sync();
	}

	if (is_infra_pdn(pwrctrl->pcm_flags))
		mtk_uart_restore();
}

static void spm_clean_after_wakeup(void)
{
	/* disable PCM WDT to stop count if needed */
#if SPM_PCMWDT_EN
	spm_write(SPM_PCM_CON1, CON1_CFG_KEY | (spm_read(SPM_PCM_CON1) & ~CON1_PCM_WDT_EN));
#endif

#if SPM_PCMTIMER_DIS
	spm_write(SPM_PCM_CON1, CON1_CFG_KEY | (spm_read(SPM_PCM_CON1) | CON1_PCM_TIMER_EN));
#endif

	__spm_clean_after_wakeup();
}

static wake_reason_t spm_output_wake_reason(struct wake_status *wakesta, struct pcm_desc *pcmdesc)
{
	wake_reason_t wr;

	wr = __spm_output_wake_reason(wakesta, pcmdesc, true);

#if 1
	memcpy(&suspend_info[log_wakesta_cnt], wakesta, sizeof(struct wake_status));
	suspend_info[log_wakesta_cnt].log_index = log_wakesta_index;

	if (log_wakesta_cnt >= 10) {
		log_wakesta_cnt = 0;
		spm_snapshot_golden_setting = 0;
	} else {
		log_wakesta_cnt++;
		log_wakesta_index++;
	}

	if (log_wakesta_index >= 0xFFFFFFF0)
		log_wakesta_index = 0;
#endif

	spm_warn("suspend dormant state = %d\n", spm_dormant_sta);
	spm_warn("log_wakesta_index = %d\n", log_wakesta_index);

	if (wakesta->r12 & WAKE_SRC_EINT)
		mt_eint_print_status();

	return wr;
}

#if SPM_PWAKE_EN
static u32 spm_get_wake_period(int pwake_time, wake_reason_t last_wr)
{
	int period = SPM_WAKE_PERIOD;
#if 1
	if (pwake_time < 0) {
		/* use FG to get the period of 1% battery decrease */
		period = get_dynamic_period(last_wr != WR_PCM_TIMER ? 1 : 0, SPM_WAKE_PERIOD, 1);
		if (period <= 0) {
			spm_warn("CANNOT GET PERIOD FROM FUEL GAUGE\n");
			period = SPM_WAKE_PERIOD;
		}
	} else {
		period = pwake_time;
		spm_crit2("pwake = %d\n", pwake_time);
	}

	if (period > 36 * 3600)	/* max period is 36.4 hours */
		period = 36 * 3600;
#endif
	return period;
}
#endif

/*
 * wakesrc: WAKE_SRC_XXX
 * enable : enable or disable @wakesrc
 * replace: if true, will replace the default setting
 */
int spm_set_sleep_wakesrc(u32 wakesrc, bool enable, bool replace)
{
	unsigned long flags;

	if (spm_is_wakesrc_invalid(wakesrc))
		return -EINVAL;

	spin_lock_irqsave(&__spm_lock, flags);
	if (enable) {
		if (replace)
			__spm_suspend.pwrctrl->wake_src = wakesrc;
		else
			__spm_suspend.pwrctrl->wake_src |= wakesrc;
	} else {
		if (replace)
			__spm_suspend.pwrctrl->wake_src = 0;
		else
			__spm_suspend.pwrctrl->wake_src &= ~wakesrc;
	}
	spin_unlock_irqrestore(&__spm_lock, flags);

	return 0;
}

/*
 * wakesrc: WAKE_SRC_XXX
 */
u32 spm_get_sleep_wakesrc(void)
{
	return __spm_suspend.pwrctrl->wake_src;
}

#if SPM_AEE_RR_REC
void spm_suspend_aee_init(void)
{
	aee_rr_rec_spm_suspend_val(0);
}
#endif

wake_reason_t spm_go_to_sleep(u32 spm_flags, u32 spm_data)
{
	u32 sec = 2;
/*	int wd_ret; */
	unsigned long flags;
	struct mtk_irq_mask mask;
	struct irq_desc *desc = irq_to_desc(spm_irq_0);
/*	struct wd_api *wd_api; */
	static wake_reason_t last_wr = WR_NONE;
	struct pcm_desc *pcmdesc;
	struct pwr_ctrl *pwrctrl;

#if SPM_AEE_RR_REC
	spm_suspend_aee_init();
	aee_rr_rec_spm_suspend_val(1 << SPM_SUSPEND_ENTER);
#endif

	pcmdesc = __spm_suspend.pcmdesc;
	pwrctrl = __spm_suspend.pwrctrl;

	set_pwrctrl_pcm_flags(pwrctrl, spm_flags);
	set_pwrctrl_pcm_data(pwrctrl, spm_data);

#if SPM_PWAKE_EN
	sec = spm_get_wake_period(-1 /* FIXME */, last_wr);
#endif
	pwrctrl->timer_val = sec * 32768;
#if 0
	wd_ret = get_wd_api(&wd_api);
	if (!wd_ret)
		wd_api->wd_suspend_notify();
#endif
/*	mt_power_gs_dump_suspend(); */

	/* spm_suspend_pre_process(pwrctrl); */
	lockdep_off();
	spin_lock_irqsave(&__spm_lock, flags);
	mt_irq_mask_all(&mask);
	if (desc)
		unmask_irq(desc);
	mt_cirq_clone_gic();
	mt_cirq_enable();

	spm_set_sysclk_settle();

	spm_warn("sec = %u, wakesrc = 0x%x (%u)(%u)\n",
		  sec, pwrctrl->wake_src, is_cpu_pdn(pwrctrl->pcm_flags),
		  is_infra_pdn(pwrctrl->pcm_flags));

	if (request_uart_to_sleep()) {
		last_wr = WR_UART_BUSY;
		goto RESTORE_IRQ;
	}

	__spm_reset_and_init_pcm(pcmdesc);

	__spm_kick_im_to_fetch(pcmdesc);

	__spm_init_pcm_register();

	__spm_init_event_vector(pcmdesc);

	__spm_set_power_control(pwrctrl);

	__spm_set_wakeup_event(pwrctrl);

	spm_kick_pcm_to_run(pwrctrl);

#if SPM_AEE_RR_REC
	aee_rr_rec_spm_suspend_val(aee_rr_curr_spm_suspend_val() | (1 << SPM_SUSPEND_ENTER_WFI));
#endif

	spm_trigger_wfi_for_sleep(pwrctrl);

#if SPM_AEE_RR_REC
	aee_rr_rec_spm_suspend_val(aee_rr_curr_spm_suspend_val() | (1 << SPM_SUSPEND_LEAVE_WFI));
#endif

	/* record last wakesta */
	__spm_get_wakeup_status(&spm_wakesta);

	spm_clean_after_wakeup();

	request_uart_to_wakeup();

	/* record last wakesta */
	/* last_wr = spm_output_wake_reason(&wakesta, pcmdesc); */
	last_wr = spm_output_wake_reason(&spm_wakesta, pcmdesc);

RESTORE_IRQ:
	mt_cirq_flush();
	mt_cirq_disable();

	mt_irq_mask_restore(&mask);

	spin_unlock_irqrestore(&__spm_lock, flags);
	lockdep_on();
	/* spm_suspend_post_process(pwrctrl); */
#if 0
	if (!wd_ret)
		wd_api->wd_resume_notify();
#endif
#if SPM_AEE_RR_REC
	aee_rr_rec_spm_suspend_val(aee_rr_curr_spm_suspend_val() | (1 << SPM_SUSPEND_LEAVE));
#endif

	return last_wr;
}

bool spm_is_conn_sleep(void)
{
	return !(spm_read(SPM_PCM_REG13_DATA) & R13_CONN_SRCLKENA);
}

void spm_set_wakeup_src_check(void)
{
	/* clean wakeup event raw status */
	spm_write(SPM_SLEEP_WAKEUP_EVENT_MASK, 0xFFFFFFFF);

	/* set wakeup event */
	spm_write(SPM_SLEEP_WAKEUP_EVENT_MASK, ~WAKE_SRC_FOR_SUSPEND);
}

bool spm_check_wakeup_src(void)
{
	u32 wakeup_src;

	/* check wanek event raw status */
	wakeup_src = spm_read(SPM_SLEEP_ISR_RAW_STA);

	if (wakeup_src) {
		spm_crit2("WARNING: spm_check_wakeup_src = 0x%x", wakeup_src);
		return 1;
	} else
		return 0;
}

void spm_poweron_config_set(void)
{
	unsigned long flags;

	spin_lock_irqsave(&__spm_lock, flags);
	/* enable register control */
	spm_write(SPM_POWERON_CONFIG_SET, (SPM_PROJECT_CODE << 16) | (1U << 0));
	spin_unlock_irqrestore(&__spm_lock, flags);
}

void spm_output_sleep_option(void)
{
	spm_notice("PWAKE_EN:%d, PCMWDT_EN:%d, BYPASS_SYSPWREQ:%d, I2C_CHANNEL:%d\n",
		   SPM_PWAKE_EN, SPM_PCMWDT_EN, SPM_BYPASS_SYSPWREQ, I2C_CHANNEL);
}

/* record last wakesta */
u32 spm_get_last_wakeup_src(void)
{
	return spm_wakesta.r12;
}

u32 spm_get_last_wakeup_misc(void)
{
	return spm_wakesta.wake_misc;
}

MODULE_DESCRIPTION("SPM-Sleep Driver v0.1");
