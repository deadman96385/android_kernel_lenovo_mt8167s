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
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#endif

#include <mt-plat/mtk_cirq.h>
#include "mtk_cpuidle.h"
#include "mtk_spm_idle.h"
#include "mtk_spm_internal.h"

/**************************************
 * only for internal debug
 **************************************/
#define SODI_TAG     "[SODI] "
#define sodi_warn(fmt, args...)		pr_warn(SODI_TAG fmt, ##args)
#define sodi_debug(fmt, args...)	pr_debug(SODI_TAG fmt, ##args)

#define SPM_BYPASS_SYSPWREQ         0	/* JTAG is used */

#define REDUCE_SODI_LOG             1
#if REDUCE_SODI_LOG
#define LOG_BUF_SIZE					256
#define SODI_LOGOUT_INTERVAL_CRITERIA	(5000U)	/* unit:ms */
#define SODI_LOGOUT_TIMEOUT_CRITERA		20
#define SODI_CONN2AP_CNT_CRITERA		20
#endif

#define SPM_USE_TWAM_DEBUG	        0

#define WAKE_SRC_FOR_SODI \
	(WAKE_SRC_KP | WAKE_SRC_GPT | WAKE_SRC_EINT | WAKE_SRC_WDT | \
	WAKE_SRC_CONN2AP | WAKE_SRC_USB_CD | WAKE_SRC_USB_PDN | WAKE_SRC_AFE | \
	WAKE_SRC_CIRQ | WAKE_SRC_SEJ | WAKE_SRC_SYSPWREQ | WAKE_SRC_CONN_WDT)

#ifdef CONFIG_MTK_RAM_CONSOLE
#define SPM_AEE_RR_REC 1
#else
#define SPM_AEE_RR_REC 0
#endif

#if SPM_AEE_RR_REC
enum spm_sodi_step {
	SPM_SODI_ENTER = 0,
	SPM_SODI_ENTER_SPM_FLOW,
	SPM_SODI_ENTER_WFI,
	SPM_SODI_LEAVE_WFI,
	SPM_SODI_LEAVE_SPM_FLOW,
	SPM_SODI_LEAVE,
	SPM_SODI_VCORE_HPM,
	SPM_SODI_VCORE_LPM
};
#endif

static const u32 sodi_binary[] = {
	0x803d0400, 0x803b0400, 0xa1d58407, 0x81f68407, 0x803a0400, 0x1b80001f,
	0x20000000, 0x80300400, 0x80328400, 0xa1d28407, 0x81f20407, 0x80338400,
	0x80318400, 0xa15f8405, 0x82851801, 0xd80002ca, 0x17c07c1f, 0x18c0001f,
	0x10006234, 0xc0c02440, 0x1200041f, 0xa15e8405, 0x81f00407, 0xc28031e0,
	0x1290041f, 0x82069801, 0xd8000828, 0x17c07c1f, 0xe8208000, 0x10210004,
	0x00001d00, 0xe8208000, 0x10210008, 0x00912345, 0xe8208000, 0x10210000,
	0x00080401, 0x1b80001f, 0x20000032, 0x1a10001f, 0x10210000, 0xe8208000,
	0x10210c40, 0x00009f8b, 0xe8208000, 0x10210c44, 0x00000902, 0xe8208000,
	0x10210c48, 0x00000109, 0xe8208000, 0x10210c4c, 0x00000000, 0xe8208000,
	0x10210c40, 0x00089f8b, 0xe8208000, 0x10210004, 0x00001d00, 0x1b80001f,
	0x200000c8, 0x1a10001f, 0x10210c40, 0x1a10001f, 0x10210c50, 0x1b00001f,
	0x7fffd7ff, 0xe8208000, 0x10000054, 0x00400000, 0xf0000000, 0x17c07c1f,
	0x1b00001f, 0x3fffc7ff, 0xd8000dac, 0x17c07c1f, 0xc0c029e0, 0x17c07c1f,
	0xe8208000, 0x10000084, 0x00400000, 0x82851801, 0xd8000b2a, 0x17c07c1f,
	0x18c0001f, 0x10006234, 0xc0c025c0, 0x17c07c1f, 0xa15e0405, 0xc28031e0,
	0x1290841f, 0xa0138400, 0x1b00001f, 0x3fffcfff, 0xa0128400, 0xa0118400,
	0x81f58407, 0xa1d68407, 0x82069801, 0x82471801, 0x82a02401, 0xa15f0405,
	0xa1d20407, 0x81f28407, 0xa0100400, 0xd8000da8, 0xa01a0400, 0xa01d2400,
	0xa01b2800, 0xf0000000, 0x17c07c1f, 0x82059801, 0xd8000ea8, 0x17c07c1f,
	0xa15f8405, 0x80318400, 0x80328400, 0x1b00001f, 0x7fffd7ff, 0xf0000000,
	0x17c07c1f, 0x82059801, 0xd8001068, 0x17c07c1f, 0xa15f0405, 0xa0128400,
	0xa0118400, 0xc28031e0, 0x1290841f, 0xc28031e0, 0x129f041f, 0x1b00001f,
	0x3fffcfff, 0xf0000000, 0x17c07c1f, 0x81f30407, 0x82831801, 0xd800190a,
	0x17c07c1f, 0x82839801, 0xd800140a, 0x17c07c1f, 0x1a00001f, 0x10006240,
	0xe2200016, 0xe220001e, 0x1b80001f, 0x2000001a, 0xe220000e, 0xe220000f,
	0xe220000d, 0x1a00001f, 0x10006c60, 0x1a50001f, 0x10006c60, 0xa2540409,
	0xe2000009, 0x1b80001f, 0x20000300, 0xa15d8405, 0xa2548409, 0xe2000009,
	0x1b80001f, 0x2000001a, 0x81fa8407, 0x1b80001f, 0x2000001a, 0x81f80407,
	0x1b80001f, 0x2000001a, 0x1b80001f, 0x20000004, 0x81f88407, 0x1b80001f,
	0x2000001a, 0x1b80001f, 0x20000004, 0x80370400, 0x1b80001f, 0x2000001a,
	0x80310400, 0x1b80001f, 0x20000004, 0x1b80001f, 0x2000001a, 0xe8208000,
	0x102111a4, 0x0ffffffc, 0xe8208000, 0x102121a4, 0x0ffffffc, 0x1b80001f,
	0x2000001a, 0x80368400, 0x81fa0407, 0x81f08407, 0x821a840d, 0xd8001888,
	0x17c07c1f, 0xa15c8405, 0xe8208000, 0x10006354, 0xfffff423, 0xa1df0407,
	0xc28031e0, 0x1291041f, 0x1b00001f, 0xbfffc7ff, 0xf0000000, 0x17c07c1f,
	0x1b80001f, 0x20000a50, 0x1a50001f, 0x10006608, 0x820ba401, 0x8217a00d,
	0xd8201cc8, 0x17c07c1f, 0x81f08407, 0xa1df0407, 0x1b00001f, 0x3fffc7ff,
	0x1b80001f, 0x20000004, 0xd800240c, 0x17c07c1f, 0x1b00001f, 0xbfffc7ff,
	0xd800240c, 0x17c07c1f, 0x81ff0407, 0x82831801, 0xd800230a, 0x17c07c1f,
	0xc0c02880, 0x17c07c1f, 0xd8001b43, 0x17c07c1f, 0xa1da0407, 0xa0170400,
	0xa0168400, 0xe8208000, 0x102111a4, 0x0ffffff8, 0xe8208000, 0x102121a4,
	0x0ffffff8, 0xa0110400, 0xa1d88407, 0xa1d80407, 0xa1da8407, 0x1a00001f,
	0x10006c60, 0x1a700008, 0x17c07c1f, 0x82748409, 0xe2000009, 0xa15c0405,
	0x1ad0001f, 0x10006b00, 0x82d1840b, 0xd800204b, 0x17c07c1f, 0x82839801,
	0xd800230a, 0x17c07c1f, 0x1a00001f, 0x10006c60, 0x82740409, 0xe2000009,
	0x18c0001f, 0x10006240, 0xc0c027e0, 0x17c07c1f, 0xa15d0405, 0x1ad0001f,
	0x10006b00, 0x82d2040b, 0xd800226b, 0x17c07c1f, 0x82041801, 0xd8002388,
	0x17c07c1f, 0xa1d30407, 0xc28031e0, 0x1291841f, 0x1b00001f, 0x7fffd7ff,
	0xf0000000, 0x17c07c1f, 0xe0f07f16, 0x1380201f, 0xe0f07f1e, 0x1380201f,
	0xe0f07f0e, 0x1b80001f, 0x20000100, 0xe0f07f0c, 0xe0f07f0d, 0xe0f07e0d,
	0xf0000000, 0x17c07c1f, 0xe0f07f0d, 0xe0f07f0f, 0xe0f07f1e, 0xe0f07f12,
	0x1b80001f, 0x2000000a, 0xf0000000, 0x17c07c1f, 0xe0e00016, 0x1380201f,
	0xe0e0001e, 0x1380201f, 0xe0e0000e, 0xe0e0000c, 0xe0e0000d, 0xf0000000,
	0x17c07c1f, 0xe0e0000f, 0xe0e0001e, 0xe0e00012, 0xf0000000, 0x17c07c1f,
	0x1212841f, 0xa1d08407, 0xd8202948, 0x80eab401, 0xd80028c3, 0x02200408,
	0x1a00001f, 0x10006814, 0xe2000003, 0xf0000000, 0x17c07c1f, 0xa1d00407,
	0x1b80001f, 0x20000100, 0x80ea3401, 0x1a00001f, 0x10006814, 0xe2000003,
	0xf0000000, 0x17c07c1f, 0xd8002bca, 0x17c07c1f, 0xe2e00036, 0xe2e0003e,
	0x1380201f, 0xe2e0003c, 0xd8202d0a, 0x17c07c1f, 0x1b80001f, 0x20000018,
	0xe2e0007c, 0x1b80001f, 0x20000003, 0xe2e0005c, 0xe2e0004c, 0xe2e0004d,
	0xf0000000, 0x17c07c1f, 0xa1d40407, 0x1391841f, 0xf0000000, 0x17c07c1f,
	0xd8002e6a, 0x17c07c1f, 0xe2e0004f, 0xe2e0006f, 0xe2e0002f, 0xd8202f0a,
	0x17c07c1f, 0xe2e0002e, 0xe2e0003e, 0xe2e00032, 0xf0000000, 0x17c07c1f,
	0x82481801, 0xd8203029, 0x17c07c1f, 0x1b80001f, 0x20000050, 0xd80030c9,
	0x17c07c1f, 0x18d0001f, 0x10006604, 0x10cf8c1f, 0xd8202f43, 0x17c07c1f,
	0xf0000000, 0x17c07c1f, 0x1ad0001f, 0x10006b00, 0x82c0280b, 0xd800310b,
	0x17c07c1f, 0xf0000000, 0x17c07c1f, 0x1a00001f, 0x10006b18, 0x1a50001f,
	0x10006b18, 0xa2402809, 0xe2000009, 0xf0000000, 0x17c07c1f, 0xe2200000,
	0x1a700008, 0x17c07c1f, 0x17c07c1f, 0x82482401, 0xd8003309, 0x17c07c1f,
	0xf0000000, 0x17c07c1f, 0xe2200004, 0x1a700008, 0x17c07c1f, 0x17c07c1f,
	0x82482401, 0xd8203429, 0x17c07c1f, 0xf0000000, 0x17c07c1f, 0xe220004f,
	0xe220000f, 0xe220002f, 0xe220002e, 0xe220003e, 0xe2200032, 0xf0000000,
	0x17c07c1f, 0xe2200036, 0x1a50001f, 0x1000660c, 0x82400809, 0xd8203649,
	0x17c07c1f, 0xe220003e, 0x1a50001f, 0x10006610, 0x82400809, 0xd8203709,
	0x17c07c1f, 0xe220007e, 0xe220007c, 0xe220005c, 0xe220004c, 0xe220004d,
	0xf0000000, 0x17c07c1f, 0xc7c03520, 0x17c07c1f, 0xe2400001, 0x1259081f,
	0x1a10001f, 0x1000660c, 0x82002408, 0xd8203908, 0x17c07c1f, 0x1a10001f,
	0x10006c60, 0xa2002808, 0x1a40001f, 0x10006c60, 0xe2400008, 0xf0000000,
	0x17c07c1f, 0xc7c03520, 0x17c07c1f, 0xd8003c6a, 0x1254041f, 0xe8208000,
	0x10006248, 0x00000000, 0x1a10001f, 0x10006248, 0x82002408, 0xd8003b88,
	0x17c07c1f, 0xf0000000, 0x17c07c1f, 0xe8208000, 0x10006244, 0x00000001,
	0x1a10001f, 0x10006244, 0x82002408, 0xd8203cc8, 0x17c07c1f, 0xf0000000,
	0x17c07c1f, 0x18d0001f, 0x10006c60, 0x82800c0a, 0x18c0001f, 0x10006c60,
	0xe0c0000a, 0xe240001f, 0x1259081f, 0x1a90001f, 0x1000660c, 0x82802809,
	0xd8003eaa, 0x17c07c1f, 0xc7c03620, 0x17c07c1f, 0xf0000000, 0x17c07c1f,
	0xd800414a, 0x1254041f, 0xe8208000, 0x10006248, 0x00000001, 0x1a90001f,
	0x10006248, 0x8280240a, 0xd820406a, 0x17c07c1f, 0xd0004240, 0x17c07c1f,
	0xe8208000, 0x10006244, 0x00000000, 0x1a90001f, 0x10006244, 0x8280240a,
	0xd80041aa, 0x17c07c1f, 0xc7c03620, 0x17c07c1f, 0xf0000000, 0x17c07c1f,
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
	0x11407c1f, 0x1b00001f, 0x3fffc7ff, 0x1b80001f, 0xd00f0000, 0xd8009c6c,
	0x17c07c1f, 0xa1578405, 0xe8208000, 0x10006354, 0xfffff423, 0x82059801,
	0xd8208328, 0x17c07c1f, 0xe8208000, 0x10006354, 0xfffff421, 0xa1d10407,
	0x1b80001f, 0x20000020, 0x82801801, 0xd800888a, 0x17c07c1f, 0x81f60407,
	0x1a00001f, 0x10006200, 0x1a40001f, 0x1000625c, 0x1096841f, 0xc2c03880,
	0x1290041f, 0xa1580405, 0xc2003100, 0x1290041f, 0x82809801, 0xd800888a,
	0x17c07c1f, 0x1a00001f, 0x10006208, 0xc2c03aa0, 0x82811801, 0xa1590405,
	0xc2003100, 0x1290841f, 0x82819801, 0xd800888a, 0x17c07c1f, 0x1a00001f,
	0x10006290, 0xc2003520, 0x17c07c1f, 0x1b80001f, 0x20000027, 0x82811801,
	0xa1d1a807, 0xa15a0405, 0xc2003100, 0x1291041f, 0xc28031e0, 0x1292041f,
	0x1a00001f, 0x10006294, 0xe8208000, 0x10006294, 0x0003ffff, 0xe2203fff,
	0xe22003ff, 0x82021801, 0xd8008b28, 0x17c07c1f, 0x1a10001f, 0x10000004,
	0x1a40001f, 0x10000004, 0x82378408, 0xe2400008, 0x1b80001f, 0x2000000a,
	0xa1d40407, 0x1b80001f, 0x20000003, 0x82049801, 0xd8008c48, 0x17c07c1f,
	0xe8208000, 0x10006604, 0x00000001, 0xc0c02f40, 0x17c07c1f, 0xa15b0405,
	0x82879801, 0xd8008eaa, 0x17c07c1f, 0xa1d38407, 0x82829801, 0xd8008d4a,
	0x17c07c1f, 0xa1d98407, 0xa0108400, 0xa0120400, 0xa0148400, 0xa0188400,
	0xd8008eaa, 0x17c07c1f, 0xa0150400, 0xa0158400, 0xa01f0400, 0xa0190400,
	0xa0198400, 0xe8208000, 0x10006310, 0x0b160008, 0xe8208000, 0x10006c00,
	0x0000000f, 0x1b00001f, 0xbfffc7ff, 0x82861801, 0xb2859941, 0xd820910a,
	0x17c07c1f, 0xe8208000, 0x10006c00, 0x0000003c, 0x1b80001f, 0x90100000,
	0xd00091e0, 0x17c07c1f, 0x1b80001f, 0x90100000, 0x82000001, 0xc8c00008,
	0x17c07c1f, 0xd0009240, 0x17c07c1f, 0x82028001, 0xc8c00de8, 0x17c07c1f,
	0x820f1c01, 0xc8e010e8, 0x17c07c1f, 0x1b00001f, 0x3fffc7ff, 0x1a00001f,
	0x10006294, 0xe22007fe, 0xe2200ffc, 0xe2201ff8, 0xe2203ff0, 0xe2203fe0,
	0xe2203fc0, 0x1b80001f, 0x20000020, 0xe8208000, 0x10006294, 0x0003ffc0,
	0xe8208000, 0x10006294, 0x0003fc00, 0x82049801, 0xd8009608, 0x17c07c1f,
	0xe8208000, 0x10006604, 0x00000000, 0xc0c02f40, 0x17c07c1f, 0xa15b8405,
	0x80388400, 0x80390400, 0x80398400, 0x803f0400, 0x1b80001f, 0x20000300,
	0x80348400, 0x80350400, 0x80358400, 0x1b80001f, 0x20000104, 0x10007c1f,
	0x81f38407, 0x81f98407, 0x81f40407, 0x82801801, 0xd8009c6a, 0x17c07c1f,
	0x82809801, 0xd8009aaa, 0x17c07c1f, 0x82819801, 0xd80099ea, 0x17c07c1f,
	0x82811801, 0x81f1a807, 0x1a00001f, 0x10006290, 0xc2003620, 0x1097841f,
	0xa15a8405, 0x1a00001f, 0x10006208, 0x1094841f, 0xc2c03fc0, 0x82811801,
	0xa1598405, 0x1a00001f, 0x10006200, 0x1a40001f, 0x1000625c, 0x1096841f,
	0xc2c03da0, 0xa2b0041f, 0xa1588405, 0x1a10001f, 0x10000004, 0x1a40001f,
	0x10000004, 0xa2178408, 0xe2400008, 0x82079401, 0xd8009f68, 0x17c07c1f,
	0x82001801, 0xd8009f68, 0x17c07c1f, 0xa1570405, 0x13007c1f, 0x1b80001f,
	0xd00f0000, 0x1a00001f, 0x10006200, 0x1a40001f, 0x1000625c, 0x1096841f,
	0xc2c03880, 0x1290041f, 0x1a00001f, 0x10006200, 0x1a40001f, 0x1000625c,
	0x1096841f, 0xc2c03da0, 0xa2b0041f, 0xa1db0407, 0x81f10407, 0xa1d60407,
	0x81f48407, 0x1ac0001f, 0x55aa55aa, 0xf0000000
};
static struct pcm_desc sodi_pcm = {
	.version	= "SUSPEND_v0.4_9_1",
	.base		= sodi_binary,
	.size		= 1282,
	.sess		= 2,
	.replace	= 0,
	.addr_2nd	= 0,
	.vec0		= EVENT_VEC(11, 1, 0, 0),	/* FUNC_26M_WAKEUP */
	.vec1		= EVENT_VEC(12, 1, 0, 72),	/* FUNC_26M_SLEEP */
	.vec2		= EVENT_VEC(30, 1, 0, 135),	/* FUNC_APSRC_WAKEUP */
	.vec3		= EVENT_VEC(31, 1, 0, 210),	/* FUNC_APSRC_SLEEP */
	.vec4		= EVENT_VEC(11, 1, 0, 111),	/* FUNC_26MIDLE_WAKEUP */
	.vec5		= EVENT_VEC(12, 1, 0, 121),	/* FUNC_26MIDLE_SLEEP */
};

static struct pwr_ctrl sodi_ctrl = {
	.wake_src		= WAKE_SRC_FOR_SODI,
	.r0_ctrl_en		= 1,
	.r7_ctrl_en		= 1,
	.infra_dcm_lock = 1,
	.wfi_op			= WFI_OP_AND,
	.ca7_wfi0_en	= 1,
	.ca7_wfi1_en	= 1,
	.ca7_wfi2_en	= 1,
	.ca7_wfi3_en	= 1,
	.mfg_req_mask	= 1,
#if SPM_BYPASS_SYSPWREQ
	.syspwreq_mask	= 1,
#endif
};

struct spm_lp_scen __spm_sodi = {
	.pcmdesc = &sodi_pcm,
	.pwrctrl = &sodi_ctrl,
};

/* 0:power-down mode, 1:CG mode */
static bool gSpm_SODI_mempll_pwr_mode = 1;
static bool gSpm_sodi_en;

#if REDUCE_SODI_LOG
static unsigned long int sodi_logout_prev_time;
static int memPllCG_prev_status = 1;	/* 1:CG, 0:pwrdn */
static unsigned int sodi_cnt, timeout_cnt;
static unsigned int refresh_cnt, not_refresh_cnt;
static unsigned int by_cldma_cnt, by_conn2ap_count;

static const char *sodi_wakesrc_str[32] = {
	[0] = "SPM_MERGE",
	[1] = "AUDIO_REQ",
	[2] = "KP",
	[3] = "WDT",
	[4] = "GPT",
	[5] = "EINT",
	[6] = "CONN_WDT",
	[7] = "GCE",
	[9] = "LOW_BAT",
	[10] = "CONN2AP",
	[11] = "F26M_WAKE",
	[12] = "F26M_SLEE",
	[13] = "PCM_WDT",
	[14] = "USB_CD ",
	[15] = "USB_PDN",
	[18] = "DBGSYS",
	[19] = "UART0",
	[20] = "AFE",
	[21] = "THERM",
	[22] = "CIRQ",
	[23] = "SEJ",
	[24] = "SYSPWREQ",
	[26] = "CPU0_IRQ",
	[27] = "CPU1_IRQ",
	[28] = "CPU2_IRQ",
	[29] = "CPU3_IRQ",
	[30] = "APSRC_WAKE",
	[31] = "APSRC_SLEEP",
};
#endif

#if SPM_AEE_RR_REC
void __attribute__((weak)) aee_rr_rec_sodi_val(u32 val)
{
}

u32 __attribute__((weak)) aee_rr_curr_sodi_val(void)
{
	return 0;
}
#endif

#if REDUCE_SODI_LOG
static long int idle_get_current_time_ms(void)
{
	struct timeval t;

	do_gettimeofday(&t);
	return ((t.tv_sec & 0xFFF) * 1000000 + t.tv_usec) / 1000;
}
#endif

static void spm_trigger_wfi_for_sodi(struct pwr_ctrl *pwrctrl)
{
	if (is_cpu_pdn(pwrctrl->pcm_flags))
		mt_cpu_dormant(CPU_SODI_MODE);
	else
		wfi_with_sync();
}

static void spm_sodi_pre_process(void)
{
	/* set PMIC WRAP table for deepidle power control */
	__spm_set_pmic_phase(PMIC_WRAP_PHASE_SODI);
}

#if 0
static void spm_sodi_post_process(void)
{
	/* set PMIC WRAP table for normal power control */
	__spm_set_pmic_phase(PMIC_WRAP_PHASE_NORMAL);
}
#endif

void spm_go_to_sodi(u32 spm_flags, u32 spm_data)
{
	struct wake_status wakesta;
	unsigned long flags;
	struct mtk_irq_mask mask;
	struct irq_desc *desc = irq_to_desc(spm_irq_0);
	wake_reason_t wr = WR_NONE;
	struct pcm_desc *pcmdesc = __spm_sodi.pcmdesc;
	struct pwr_ctrl *pwrctrl = __spm_sodi.pwrctrl;
	int vcore_status = 0;	/* 0:disable, 1:HPM, 2:LPM */
#if REDUCE_SODI_LOG
	unsigned long int sodi_logout_curr_time = 0;
	int need_log_out = 0;
#endif

#if SPM_AEE_RR_REC
	aee_rr_rec_sodi_val(1 << SPM_SODI_ENTER);
#endif

	spm_flags |= (SPM_BUS26M_DIS | SPM_FHC_SLEEP_DIS | SPM_26M_DIS | SPM_MCU_PDN_DIS);

	if (gSpm_SODI_mempll_pwr_mode == 1)
		spm_flags |= SPM_MPLLOFF_DIS;	/* MEMPLL CG mode */
	else
		spm_flags &= ~SPM_MPLLOFF_DIS;	/* DDRPHY power down mode */

	set_pwrctrl_pcm_flags(pwrctrl, spm_flags);

	/* enable APxGPT timer */
	soidle_before_wfi(0);

	lockdep_off();
	spin_lock_irqsave(&__spm_lock, flags);

	mt_irq_mask_all(&mask);
	if (desc)
		unmask_irq(desc);
	mt_cirq_clone_gic();
	mt_cirq_enable();

#if SPM_AEE_RR_REC
	aee_rr_rec_sodi_val(aee_rr_curr_sodi_val() | (1 << SPM_SODI_ENTER_SPM_FLOW));
#endif

	__spm_reset_and_init_pcm(pcmdesc);

	__spm_kick_im_to_fetch(pcmdesc);

	__spm_init_pcm_register();

	__spm_init_event_vector(pcmdesc);

	/* Display set SPM_PCM_SRC_REQ[0]=1'b1 to force DRAM not enter self-refresh mode */
	if ((spm_read(SPM_PCM_SRC_REQ) & 0x00000001))
		pwrctrl->pcm_apsrc_req = 1;
	else
		pwrctrl->pcm_apsrc_req = 0;

	__spm_set_power_control(pwrctrl);

	__spm_set_wakeup_event(pwrctrl);

	spm_sodi_pre_process();

	__spm_kick_pcm_to_run(pwrctrl);

#if SPM_AEE_RR_REC
	aee_rr_rec_sodi_val(aee_rr_curr_sodi_val() | (1 << SPM_SODI_ENTER_WFI));
#endif

	spm_trigger_wfi_for_sodi(pwrctrl);

#if SPM_AEE_RR_REC
	aee_rr_rec_sodi_val(aee_rr_curr_sodi_val() | (1 << SPM_SODI_LEAVE_WFI));
#endif

	/* spm_sodi_post_process(); */

	__spm_get_wakeup_status(&wakesta);

	__spm_clean_after_wakeup();

#if REDUCE_SODI_LOG == 0
	sodi_debug("emi-selfrefrsh cnt = %d, pcm_flag = 0x%x, SPM_PCM_RESERVE2 = 0x%x, %s\n",
		   spm_read(SPM_PCM_PASR_DPD_3), spm_read(SPM_PCM_FLAGS),
		   spm_read(SPM_PCM_RESERVE2), pcmdesc->version);

	wr = __spm_output_wake_reason(&wakesta, pcmdesc, false);
	if (wr == WR_PCM_ASSERT) {
		sodi_warn("PCM ASSERT AT %u (%s), r13 = 0x%x, debug_flag = 0x%x\n",
			 wakesta.assert_pc, pcmdesc->version, wakesta.r13, wakesta.debug_flag);
	} else if (wakesta.r12 == 0) {
		sodi_warn("Warning: not wakeup by SPM, r13 = 0x%x, debug_flag = 0x%x\n",
			 wakesta.r13, wakesta.debug_flag);
	}
#else
	sodi_logout_curr_time = idle_get_current_time_ms();

	if (wakesta.assert_pc != 0 || wakesta.r12 == 0) {
		need_log_out = 1;
	} else if (wakesta.r12 == 0) {
		need_log_out = 2;
	} else if ((wakesta.r12 & (WAKE_SRC_GPT | WAKE_SRC_CONN2AP)) == 0) {
		/* not wakeup by GPT, CONN2AP and CLDMA_WDT */
		need_log_out = 3;
	} else if (wakesta.r12 & WAKE_SRC_CONN2AP) {
		/* wake up by WAKE_SRC_CONN2AP */
		if (by_conn2ap_count == 0)
			need_log_out = 4;

		if (by_conn2ap_count >= SODI_CONN2AP_CNT_CRITERA)
			by_conn2ap_count = 0;
		else
			by_conn2ap_count++;
	} else if ((sodi_logout_curr_time - sodi_logout_prev_time) > SODI_LOGOUT_INTERVAL_CRITERIA) {
		need_log_out = 5;
	} else {		/* check CG/pwrdn status is changed */

		int mem_status = 0;

		/* check mempll CG/pwrdn status change */
		if (((spm_read(SPM_PCM_FLAGS) & 0x40) != 0)
		    || ((spm_read(SPM_PCM_RESERVE2) & 0x80) != 0)) {
			mem_status = 1;
		}

		if (memPllCG_prev_status != mem_status) {
			memPllCG_prev_status = mem_status;
			need_log_out = 6;
		}
	}

	if (need_log_out) {
		sodi_logout_prev_time = sodi_logout_curr_time;

		if (wakesta.assert_pc != 0 || wakesta.r12 == 0) {
			sodi_warn("Warning: %s, self-refrsh = %d, pcm_flag = 0x%x, PCM_RSV2 = 0x%x, vcore_status = %d\n",
			     (wakesta.assert_pc != 0) ? "wakeup by SPM assert" : "not wakeup by SPM",
			     spm_read(SPM_PCM_PASR_DPD_3), spm_read(SPM_PCM_FLAGS),
			     spm_read(SPM_PCM_RESERVE2), vcore_status);
			sodi_warn("sodi_cnt =%d, self-refresh_cnt = %d, , spm_pc = 0x%0x, r13 = 0x%x, debug_flag = 0x%x\n",
			     sodi_cnt, refresh_cnt, wakesta.assert_pc,
			      wakesta.r13, wakesta.debug_flag);
			sodi_warn("r12 = 0x%x, raw_sta = 0x%x, idle_sta = 0x%x, event_reg = 0x%x, isr = 0x%x, %s\n",
				 wakesta.r12, wakesta.raw_sta, wakesta.idle_sta,
				 wakesta.event_reg, wakesta.isr, pcmdesc->version);
		} else {
			char buf[LOG_BUF_SIZE] = { 0 };
			int i, left_size;

			if (wakesta.r12 & WAKE_SRC_SPM_MERGE) {
				if (wakesta.wake_misc & WAKE_MISC_PCM_TIMER)
					strcat(buf, " PCM_TIMER");
				if (wakesta.wake_misc & WAKE_MISC_TWAM)
					strcat(buf, " TWAM");
				if (wakesta.wake_misc & WAKE_MISC_CPU_WAKE)
					strcat(buf, " CPU");
			}
			for (i = 1; i < 32; i++) {
				if (wakesta.r12 & (1U << i)) {
					left_size = sizeof(buf) - strlen(buf) - 1;
					if (left_size >= strlen(sodi_wakesrc_str[i]))
						strcat(buf, sodi_wakesrc_str[i]);
					else
						strncat(buf, sodi_wakesrc_str[i], left_size);

					wr = WR_WAKE_SRC;
				}
			}
			WARN_ON(strlen(buf) >= LOG_BUF_SIZE);

			sodi_debug("%d, wakeup by %s, self-refresh=%d, pcm_flag=0x%x, pcm_rsv2=0x%x, vcore=%d, debug_flag = 0x%x, timer_out=%u\n",
			     need_log_out, buf,
			     spm_read(SPM_PCM_PASR_DPD_3),
			     spm_read(SPM_PCM_FLAGS), spm_read(SPM_PCM_RESERVE2),
			     vcore_status, wakesta.debug_flag, wakesta.timer_out);

			sodi_debug("sodi_cnt=%u, not_refresh_cnt=%u, refresh_cnt=%u, avg_refresh_cnt=%u, timeout_cnt=%u, cldma_cnt=%u, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
			     sodi_cnt, not_refresh_cnt, refresh_cnt,
			     (refresh_cnt / ((sodi_cnt - not_refresh_cnt) ? (sodi_cnt - not_refresh_cnt) : 1)),
			     timeout_cnt, by_cldma_cnt,
			     wakesta.r12, wakesta.r13,
			     wakesta.raw_sta, wakesta.idle_sta, wakesta.event_reg, wakesta.isr);
		}

		sodi_cnt = 0;
		refresh_cnt = 0;
		not_refresh_cnt = 0;
		by_cldma_cnt = 0;
		timeout_cnt = 0;
	} else {
		sodi_cnt++;
		refresh_cnt += spm_read(SPM_PCM_PASR_DPD_3);

		if (spm_read(SPM_PCM_PASR_DPD_3) == 0)
			not_refresh_cnt++;

		if (wakesta.timer_out <= SODI_LOGOUT_TIMEOUT_CRITERA)
			timeout_cnt++;
	}
#endif

#if SPM_AEE_RR_REC
	aee_rr_rec_sodi_val(aee_rr_curr_sodi_val() | (1 << SPM_SODI_LEAVE_SPM_FLOW));
#endif

	mt_cirq_flush();
	mt_cirq_disable();
	mt_irq_mask_restore(&mask);

	spin_unlock_irqrestore(&__spm_lock, flags);
	lockdep_on();

	/* stop APxGPT timer and enable caore0 local timer */
	soidle_after_wfi(0);

#if SPM_AEE_RR_REC
	aee_rr_rec_sodi_val(0);
#endif
}

void spm_sodi_mempll_pwr_mode(bool pwr_mode)
{
	/* printk("[SODI]set pwr_mode = %d\n",pwr_mode); */
	gSpm_SODI_mempll_pwr_mode = pwr_mode;
}

void spm_enable_sodi(bool en)
{
	gSpm_sodi_en = en;
}

bool spm_get_sodi_en(void)
{
	return gSpm_sodi_en;
}

#if SPM_AEE_RR_REC
static void spm_sodi_aee_init(void)
{
	aee_rr_rec_sodi_val(0);
}
#endif

#if SPM_USE_TWAM_DEBUG
#define SPM_TWAM_MONITOR_TICK 333333
static void twam_handler(struct twam_sig *twamsig)
{
	spm_crit("sig_high = %u%%  %u%%  %u%%  %u%%, r13 = 0x%x\n",
		 get_percent(twamsig->sig0, SPM_TWAM_MONITOR_TICK),
		 get_percent(twamsig->sig1, SPM_TWAM_MONITOR_TICK),
		 get_percent(twamsig->sig2, SPM_TWAM_MONITOR_TICK),
		 get_percent(twamsig->sig3, SPM_TWAM_MONITOR_TICK),
			spm_read(SPM_PCM_REG13_DATA));
}
#endif

void spm_sodi_init(void)
{
#if SPM_USE_TWAM_DEBUG
	unsigned long flags;
	struct twam_sig twamsig = {
		.sig0 = 10,	/* disp_req */
		.sig1 = 23,	/* self-refresh */
		.sig2 = 25,	/* md2_srcclkena */
		.sig3 = 21,	/* md2_apsrc_req_mux */
	};
#endif

#if SPM_AEE_RR_REC
	spm_sodi_aee_init();
#endif

#if SPM_USE_TWAM_DEBUG
#if 0
	spin_lock_irqsave(&__spm_lock, flags);
	spm_write(SPM_AP_STANBY_CON, spm_read(SPM_AP_STANBY_CON) | ASC_MD_DDR_EN_SEL);
	spin_unlock_irqrestore(&__spm_lock, flags);
#endif

	spm_twam_register_handler(twam_handler);
	spm_twam_enable_monitor(&twamsig, false, SPM_TWAM_MONITOR_TICK);
#endif
}

MODULE_DESCRIPTION("SPM-SODI Driver v0.1");
