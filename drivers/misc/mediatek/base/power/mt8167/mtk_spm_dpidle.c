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

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/of_fdt.h>
#include <linux/lockdep.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#endif

/* TODO: re-enable wdt later */
/* #include <mach/wd_api.h> */
#include <mt-plat/mtk_cirq.h>

#include "mtk_spm_idle.h"
#include "mtk_cpuidle.h"

#include "mtk_spm_internal.h"

/*
 * only for internal debug
 */
#define DPIDLE_TAG     "[DP] "
#define dpidle_dbg(fmt, args...)	pr_debug(DPIDLE_TAG fmt, ##args)

#define SPM_PWAKE_EN            0
#define SPM_BYPASS_SYSPWREQ     1

#define WAKE_SRC_FOR_DPIDLE \
	(WAKE_SRC_KP | WAKE_SRC_GPT | WAKE_SRC_EINT | WAKE_SRC_CONN_WDT| \
	WAKE_SRC_CONN2AP | WAKE_SRC_USB_CD | WAKE_SRC_USB_PDN | \
	WAKE_SRC_AFE | WAKE_SRC_SYSPWREQ | WAKE_SRC_SEJ)

#define I2C_CHANNEL 2

#define spm_is_wakesrc_invalid(wakesrc)     (!!((u32)(wakesrc) & 0xc0003803))

#ifdef CONFIG_MTK_RAM_CONSOLE
#define SPM_AEE_RR_REC 1
#else
#define SPM_AEE_RR_REC 0
#endif
#define SPM_USE_TWAM_DEBUG	0

#define	DPIDLE_LOG_PRINT_TIMEOUT_CRITERIA	20
#define	DPIDLE_LOG_DISCARD_CRITERIA			5000	/* ms */

#if SPM_AEE_RR_REC
enum spm_deepidle_step {
	SPM_DEEPIDLE_ENTER = 0,
	SPM_DEEPIDLE_ENTER_UART_SLEEP,
	SPM_DEEPIDLE_ENTER_WFI,
	SPM_DEEPIDLE_LEAVE_WFI,
	SPM_DEEPIDLE_ENTER_UART_AWAKE,
	SPM_DEEPIDLE_LEAVE
};
#endif

/**********************************************************
 * PCM code for deep idle
 **********************************************************/
static const u32 suspend_binary[] = {
	0x803d0400, 0x803b0400, 0xa1d58407, 0x81f68407, 0x803a0400, 0x1b80001f,
	0x20000000, 0x80300400, 0x80328400, 0xa1d28407, 0x81f20407, 0x80338400,
	0x80318400, 0xa15f8405, 0x82851801, 0xd80002ca, 0x17c07c1f, 0x18c0001f,
	0x10006234, 0xc0c02aa0, 0x1200041f, 0xa15e8405, 0x81f00407, 0xc2803840,
	0x1290041f, 0x82069801, 0xd8000e68, 0x17c07c1f, 0x1ad0001f, 0x10006c30,
	0x8a80000b, 0x00000030, 0x1294281f, 0xa2922c0a, 0xaa80000a, 0x00000004,
	0x1a00001f, 0x10210008, 0xe8208000, 0x10210004, 0x00001d00, 0xaa40000a,
	0x00900000, 0xe2000009, 0xe8208000, 0x10210000, 0x00080401, 0x1b80001f,
	0x20000032, 0xe8208000, 0x10210004, 0x00001d00, 0xe8208000, 0x10210008,
	0x00100c00, 0xe8208000, 0x10210000, 0x00080401, 0x1b80001f, 0x20000032,
	0xe8208000, 0x10210004, 0x00001d00, 0xe8208000, 0x10210008, 0x00100c00,
	0xe8208000, 0x10210000, 0x00080401, 0x1b80001f, 0x2000079e, 0xe8208000,
	0x10210004, 0x00001d00, 0xe8208000, 0x10210008, 0x00100400, 0xe8208000,
	0x10210000, 0x00080401, 0x1b80001f, 0x20000032, 0xe8208000, 0x10210004,
	0x00001d00, 0xe8208000, 0x10210008, 0x03400048, 0xe8208000, 0x10210000,
	0x00080401, 0x1b80001f, 0x20000032, 0xe8208000, 0x10210004, 0x00001d00,
	0xe8208000, 0x10210008, 0x00400004, 0xe8208000, 0x10210000, 0x00080401,
	0x1b80001f, 0x20000032, 0xe8208000, 0x10210004, 0x00001d00, 0xe8208000,
	0x10210008, 0x00702000, 0xe8208000, 0x10210000, 0x00080401, 0x1b80001f,
	0x20000032, 0x1b00001f, 0x7fffd7ff, 0xe8208000, 0x10000054, 0x00400000,
	0xf0000000, 0x17c07c1f, 0x1b00001f, 0x3fffc7ff, 0xd80013ec, 0x17c07c1f,
	0xc0c03040, 0x17c07c1f, 0xe8208000, 0x10000084, 0x00400000, 0x82851801,
	0xd800116a, 0x17c07c1f, 0x18c0001f, 0x10006234, 0xc0c02c20, 0x17c07c1f,
	0xa15e0405, 0xc2803840, 0x1290841f, 0xa0138400, 0x1b00001f, 0x3fffcfff,
	0xa0128400, 0xa0118400, 0x81f58407, 0xa1d68407, 0x82069801, 0x82471801,
	0x82a02401, 0xa15f0405, 0xa1d20407, 0x81f28407, 0xa0100400, 0xd80013e8,
	0xa01a0400, 0xa01d2400, 0xa01b2800, 0xf0000000, 0x17c07c1f, 0x82059801,
	0xd80014e8, 0x17c07c1f, 0xa15f8405, 0x80318400, 0x80328400, 0x1b00001f,
	0x7fffd7ff, 0xf0000000, 0x17c07c1f, 0x82059801, 0xd80016a8, 0x17c07c1f,
	0xa15f0405, 0xa0128400, 0xa0118400, 0xc2803840, 0x1290841f, 0xc2803840,
	0x129f041f, 0x1b00001f, 0x3fffcfff, 0xf0000000, 0x17c07c1f, 0x81f30407,
	0x82831801, 0xd8001f0a, 0x17c07c1f, 0x82839801, 0xd8001a0a, 0x17c07c1f,
	0x1a00001f, 0x10006240, 0xe220001e, 0xe220000c, 0x1b80001f, 0x2000001a,
	0xe220000d, 0x1a00001f, 0x10006c60, 0x1a50001f, 0x10006c60, 0xa2540409,
	0xe2000009, 0x1b80001f, 0x20000300, 0xa15d8405, 0xa2548409, 0xe2000009,
	0x1b80001f, 0x2000001a, 0x81fa8407, 0x1b80001f, 0x2000001a, 0x81f80407,
	0x1b80001f, 0x2000001a, 0x1b80001f, 0x20000004, 0x81f88407, 0x1b80001f,
	0x2000001a, 0x1b80001f, 0x20000004, 0x80370400, 0x1b80001f, 0x2000001a,
	0x80310400, 0x1b80001f, 0x20000004, 0x1b80001f, 0x2000001a, 0xe8208000,
	0x102111a4, 0x0ffffffc, 0xe8208000, 0x102121a4, 0x0ffffffc, 0x1b80001f,
	0x2000001a, 0x80368400, 0x81fa0407, 0x81f08407, 0x821a840d, 0xd8001e88,
	0x17c07c1f, 0xa15c8405, 0xe8208000, 0x10006354, 0xfffff423, 0xa1df0407,
	0xc2803840, 0x1291041f, 0x1b00001f, 0xbfffc7ff, 0xf0000000, 0x17c07c1f,
	0x1b80001f, 0x20000a50, 0x1a50001f, 0x10006608, 0x820ba401, 0x8217a00d,
	0xd82022c8, 0x17c07c1f, 0x81f08407, 0xa1df0407, 0x1b00001f, 0x3fffc7ff,
	0x1b80001f, 0x20000004, 0xd8002a6c, 0x17c07c1f, 0x1b00001f, 0xbfffc7ff,
	0xd8002a6c, 0x17c07c1f, 0x81ff0407, 0x82831801, 0xd800296a, 0x17c07c1f,
	0xc0c02ee0, 0x17c07c1f, 0xd8002143, 0x17c07c1f, 0xa1da0407, 0xa0170400,
	0xa0168400, 0xe8208000, 0x102111a4, 0x0ffffff8, 0xe8208000, 0x102121a4,
	0x0ffffff8, 0xa0110400, 0xa1d88407, 0xa1d80407, 0xa1da8407, 0x1a00001f,
	0x10006c60, 0x1a700008, 0x17c07c1f, 0x82748409, 0xe2000009, 0xa15c0405,
	0x1ad0001f, 0x10006b00, 0x82d1840b, 0xd800264b, 0x17c07c1f, 0x82839801,
	0xd800296a, 0x17c07c1f, 0x1a00001f, 0x10006c60, 0x82740409, 0xe2000009,
	0x18c0001f, 0x10006240, 0xe0e0000f, 0x1b80001f, 0x20000003, 0xe0e0000c,
	0xe0e00000, 0xa15d0405, 0x1ad0001f, 0x10006b00, 0x82d2040b, 0xd80028cb,
	0x17c07c1f, 0x82041801, 0xd80029e8, 0x17c07c1f, 0xa1d30407, 0xc2803840,
	0x1291841f, 0x1b00001f, 0x7fffd7ff, 0xf0000000, 0x17c07c1f, 0xe0f07f16,
	0x1380201f, 0xe0f07f1e, 0x1380201f, 0xe0f07f0e, 0x1b80001f, 0x20000100,
	0xe0f07f0c, 0xe0f07f0d, 0xe0f07e0d, 0xf0000000, 0x17c07c1f, 0xe0f07f0d,
	0xe0f07f0f, 0xe0f07f1e, 0xe0f07f12, 0x1b80001f, 0x2000000a, 0xf0000000,
	0x17c07c1f, 0xe0e00016, 0x1380201f, 0xe0e0001e, 0x1380201f, 0xe0e0000e,
	0xe0e0000c, 0xe0e0000d, 0xf0000000, 0x17c07c1f, 0xe0e0000f, 0xe0e0001e,
	0xe0e00012, 0xf0000000, 0x17c07c1f, 0x1212841f, 0xa1d08407, 0xd8202fa8,
	0x80eab401, 0xd8002f23, 0x02200408, 0x1a00001f, 0x10006814, 0xe2000003,
	0xf0000000, 0x17c07c1f, 0xa1d00407, 0x1b80001f, 0x20000100, 0x80ea3401,
	0x1a00001f, 0x10006814, 0xe2000003, 0xf0000000, 0x17c07c1f, 0xd800322a,
	0x17c07c1f, 0xe2e00036, 0xe2e0003e, 0x1380201f, 0xe2e0003c, 0xd820336a,
	0x17c07c1f, 0x1b80001f, 0x20000018, 0xe2e0007c, 0x1b80001f, 0x20000003,
	0xe2e0005c, 0xe2e0004c, 0xe2e0004d, 0xf0000000, 0x17c07c1f, 0xa1d40407,
	0x1391841f, 0xf0000000, 0x17c07c1f, 0xd80034ca, 0x17c07c1f, 0xe2e0004f,
	0xe2e0006f, 0xe2e0002f, 0xd820356a, 0x17c07c1f, 0xe2e0002e, 0xe2e0003e,
	0xe2e00032, 0xf0000000, 0x17c07c1f, 0x82481801, 0xd8203689, 0x17c07c1f,
	0x1b80001f, 0x20000050, 0xd8003729, 0x17c07c1f, 0x18d0001f, 0x10006604,
	0x10cf8c1f, 0xd82035a3, 0x17c07c1f, 0xf0000000, 0x17c07c1f, 0x1ad0001f,
	0x10006b00, 0x82c0280b, 0xd800376b, 0x17c07c1f, 0xf0000000, 0x17c07c1f,
	0x1a00001f, 0x10006b18, 0x1a50001f, 0x10006b18, 0xa2402809, 0xe2000009,
	0xf0000000, 0x17c07c1f, 0xe2200000, 0x1a700008, 0x17c07c1f, 0x17c07c1f,
	0x82482401, 0xd8003969, 0x17c07c1f, 0xf0000000, 0x17c07c1f, 0xe2200004,
	0x1a700008, 0x17c07c1f, 0x17c07c1f, 0x82482401, 0xd8203a89, 0x17c07c1f,
	0xf0000000, 0x17c07c1f, 0xe220004f, 0xe220000f, 0xe220002f, 0xe220002e,
	0xe220003e, 0xe2200032, 0xf0000000, 0x17c07c1f, 0xe2200036, 0x1a50001f,
	0x1000660c, 0x82400809, 0xd8203ca9, 0x17c07c1f, 0xe220003e, 0x1a50001f,
	0x10006610, 0x82400809, 0xd8203d69, 0x17c07c1f, 0xe220007e, 0xe220007c,
	0xe220005c, 0xe220004c, 0xe220004d, 0xf0000000, 0x17c07c1f, 0xc7c03b80,
	0x17c07c1f, 0xe2400001, 0x1259081f, 0x1a10001f, 0x1000660c, 0x82002408,
	0xd8203f68, 0x17c07c1f, 0x1a10001f, 0x10006c60, 0xa2002808, 0x1a40001f,
	0x10006c60, 0xe2400008, 0xf0000000, 0x17c07c1f, 0xc7c03b80, 0x17c07c1f,
	0xd80042ca, 0x1254041f, 0xe8208000, 0x10006248, 0x00000000, 0x1a10001f,
	0x10006248, 0x82002408, 0xd80041e8, 0x17c07c1f, 0xf0000000, 0x17c07c1f,
	0xe8208000, 0x10006244, 0x00000001, 0x1a10001f, 0x10006244, 0x82002408,
	0xd8204328, 0x17c07c1f, 0xf0000000, 0x17c07c1f, 0x18d0001f, 0x10006c60,
	0x82800c0a, 0x18c0001f, 0x10006c60, 0xe0c0000a, 0xe240001f, 0x1259081f,
	0x1a90001f, 0x1000660c, 0x82802809, 0xd800450a, 0x17c07c1f, 0xc7c03c80,
	0x17c07c1f, 0xf0000000, 0x17c07c1f, 0xd80047aa, 0x1254041f, 0xe8208000,
	0x10006248, 0x00000001, 0x1a90001f, 0x10006248, 0x8280240a, 0xd82046ca,
	0x17c07c1f, 0xd00048a0, 0x17c07c1f, 0xe8208000, 0x10006244, 0x00000000,
	0x1a90001f, 0x10006244, 0x8280240a, 0xd800480a, 0x17c07c1f, 0xc7c03c80,
	0x17c07c1f, 0xf0000000, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f, 0x17c07c1f,
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
	0x1a00001f, 0x10006200, 0x1a40001f, 0x1000625c, 0x1096841f, 0xc2c03ee0,
	0x1290041f, 0xa1580405, 0xc2003760, 0x1290041f, 0x82809801, 0xd800888a,
	0x17c07c1f, 0x1a00001f, 0x10006208, 0xc2c04100, 0x82811801, 0xa1590405,
	0xc2003760, 0x1290841f, 0x82819801, 0xd800888a, 0x17c07c1f, 0x1a00001f,
	0x10006290, 0xc2003b80, 0x17c07c1f, 0x1b80001f, 0x20000027, 0x82811801,
	0xa1d1a807, 0xa15a0405, 0xc2003760, 0x1291041f, 0xc2803840, 0x1292041f,
	0x1a00001f, 0x10006294, 0xe8208000, 0x10006294, 0x0003ffff, 0xe2203fff,
	0xe22003ff, 0x82021801, 0xd8008b28, 0x17c07c1f, 0x1a10001f, 0x10000004,
	0x1a40001f, 0x10000004, 0x82378408, 0xe2400008, 0x1b80001f, 0x2000000a,
	0xa1d40407, 0x1b80001f, 0x20000003, 0x82049801, 0xd8008c48, 0x17c07c1f,
	0xe8208000, 0x10006604, 0x00000001, 0xc0c035a0, 0x17c07c1f, 0xa15b0405,
	0x82879801, 0xd8008eaa, 0x17c07c1f, 0xa1d38407, 0x82829801, 0xd8008d4a,
	0x17c07c1f, 0xa1d98407, 0xa0108400, 0xa0120400, 0xa0148400, 0xa0188400,
	0xd8008eaa, 0x17c07c1f, 0xa0150400, 0xa0158400, 0xa01f0400, 0xa0190400,
	0xa0198400, 0xe8208000, 0x10006310, 0x0b160008, 0xe8208000, 0x10006c00,
	0x0000000f, 0x1b00001f, 0xbfffc7ff, 0x82861801, 0xb2859941, 0xd820910a,
	0x17c07c1f, 0xe8208000, 0x10006c00, 0x0000003c, 0x1b80001f, 0x90100000,
	0xd00091e0, 0x17c07c1f, 0x1b80001f, 0x90100000, 0x82000001, 0xc8c00008,
	0x17c07c1f, 0xd0009240, 0x17c07c1f, 0x82028001, 0xc8c01428, 0x17c07c1f,
	0x820f1c01, 0xc8e01728, 0x17c07c1f, 0x1b00001f, 0x3fffc7ff, 0x1a00001f,
	0x10006294, 0xe22007fe, 0xe2200ffc, 0xe2201ff8, 0xe2203ff0, 0xe2203fe0,
	0xe2203fc0, 0x1b80001f, 0x20000020, 0xe8208000, 0x10006294, 0x0003ffc0,
	0xe8208000, 0x10006294, 0x0003fc00, 0x82049801, 0xd8009608, 0x17c07c1f,
	0xe8208000, 0x10006604, 0x00000000, 0xc0c035a0, 0x17c07c1f, 0xa15b8405,
	0x80388400, 0x80390400, 0x80398400, 0x803f0400, 0x1b80001f, 0x20000300,
	0x80348400, 0x80350400, 0x80358400, 0x1b80001f, 0x20000104, 0x10007c1f,
	0x81f38407, 0x81f98407, 0x81f40407, 0x82801801, 0xd8009c6a, 0x17c07c1f,
	0x82809801, 0xd8009aaa, 0x17c07c1f, 0x82819801, 0xd80099ea, 0x17c07c1f,
	0x82811801, 0x81f1a807, 0x1a00001f, 0x10006290, 0xc2003c80, 0x1097841f,
	0xa15a8405, 0x1a00001f, 0x10006208, 0x1094841f, 0xc2c04620, 0x82811801,
	0xa1598405, 0x1a00001f, 0x10006200, 0x1a40001f, 0x1000625c, 0x1096841f,
	0xc2c04400, 0xa2b0041f, 0xa1588405, 0x1a10001f, 0x10000004, 0x1a40001f,
	0x10000004, 0xa2178408, 0xe2400008, 0x82079401, 0xd8009f68, 0x17c07c1f,
	0x82001801, 0xd8009f68, 0x17c07c1f, 0xa1570405, 0x13007c1f, 0x1b80001f,
	0xd00f0000, 0x1a00001f, 0x10006200, 0x1a40001f, 0x1000625c, 0x1096841f,
	0xc2c03ee0, 0x1290041f, 0x1a00001f, 0x10006200, 0x1a40001f, 0x1000625c,
	0x1096841f, 0xc2c04400, 0xa2b0041f, 0xa1db0407, 0x81f10407, 0xa1d60407,
	0x81f48407, 0x1ac0001f, 0x55aa55aa, 0xf0000000
};

static struct pcm_desc dpidle_pcm = {
	.version	= "SUSPEND_V0.4_9_2bsi3",
	.base		= suspend_binary,
	.size		= 1282,
	.sess		= 2,
	.replace	= 0,
	.addr_2nd	= 0,
	.vec0		= EVENT_VEC(11, 1, 0, 0),	/* FUNC_26M_WAKEUP */
	.vec1		= EVENT_VEC(12, 1, 0, 122),	/* FUNC_26M_SLEEP */
	.vec2		= EVENT_VEC(30, 1, 0, 185),	/* FUNC_APSRC_WAKEUP */
	.vec3		= EVENT_VEC(31, 1, 0, 258),	/* FUNC_APSRC_SLEEP */
	.vec4		= EVENT_VEC(11, 1, 0, 161),	/* FUNC_26MIDLE_WAKEUP */
	.vec5		= EVENT_VEC(12, 1, 0, 171),	/* FUNC_26MIDLE_SLEEP */
};

static struct pwr_ctrl dpidle_ctrl = {
	.wake_src = WAKE_SRC_FOR_DPIDLE,
	.r0_ctrl_en = 1,
	.r7_ctrl_en = 1,
	.infra_dcm_lock = 1,
	.wfi_op = WFI_OP_AND,
	.ca15_wfi0_en = 1,
	.ca15_wfi1_en = 1,
	.ca15_wfi2_en = 1,
	.ca15_wfi3_en = 1,
	.ca7_wfi0_en = 1,
	.ca7_wfi1_en = 1,
	.ca7_wfi2_en = 1,
	.ca7_wfi3_en = 1,
	.disp0_req_mask = 1,
	.disp1_req_mask = 1,
	.mfg_req_mask = 1,
	.syspwreq_mask = 1,
};

static unsigned int dpidle_log_discard_cnt;
static unsigned int dpidle_log_print_prev_time;

struct spm_lp_scen __spm_dpidle = {
	.pcmdesc = &dpidle_pcm,
	.pwrctrl = &dpidle_ctrl,
};

int __attribute__ ((weak)) request_uart_to_sleep(void)
{
	return 0;
}

int __attribute__ ((weak)) request_uart_to_wakeup(void)
{
	return 0;
}

#if SPM_AEE_RR_REC
void __attribute__ ((weak)) aee_rr_rec_deepidle_val(u32 val)
{
}

u32 __attribute__ ((weak)) aee_rr_curr_deepidle_val(void)
{
	return 0;
}
#endif

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

u32 __attribute__ ((weak)) spm_get_sleep_wakesrc(void)
{
	return 0;
}

#if 0
void __attribute__ ((weak)) mt_cpufreq_set_pmic_phase(enum pmic_wrap_phase_id phase)
{
}
#endif

static long int idle_get_current_time_ms(void)
{
	struct timeval t;

	do_gettimeofday(&t);
	return ((t.tv_sec & 0xFFF) * 1000000 + t.tv_usec) / 1000;
}

static void spm_trigger_wfi_for_dpidle(struct pwr_ctrl *pwrctrl)
{
	if (is_cpu_pdn(pwrctrl->pcm_flags)) {
		mt_cpu_dormant(CPU_DEEPIDLE_MODE);
	} else {
		/*
		 * MT6735/MT6735M: Mp0_axi_config[4] is one by default. No need to program it before entering deepidle.
		 * MT6753:         Have to program it before entering deepidle.
		 */
		wfi_with_sync();
	}
}

/*
 * wakesrc: WAKE_SRC_XXX
 * enable : enable or disable @wakesrc
 * replace: if true, will replace the default setting
 */
int spm_set_dpidle_wakesrc(u32 wakesrc, bool enable, bool replace)
{
	unsigned long flags;

	if (spm_is_wakesrc_invalid(wakesrc))
		return -EINVAL;

	spin_lock_irqsave(&__spm_lock, flags);
	if (enable) {
		if (replace)
			__spm_dpidle.pwrctrl->wake_src = wakesrc;
		else
			__spm_dpidle.pwrctrl->wake_src |= wakesrc;
	} else {
		if (replace)
			__spm_dpidle.pwrctrl->wake_src = 0;
		else
			__spm_dpidle.pwrctrl->wake_src &= ~wakesrc;
	}
	spin_unlock_irqrestore(&__spm_lock, flags);

	return 0;
}

static wake_reason_t spm_output_wake_reason(struct wake_status *wakesta, struct pcm_desc *pcmdesc, u32 dump_log)
{
	wake_reason_t wr = WR_NONE;
	unsigned long int dpidle_log_print_curr_time = 0;
	bool log_print = false;
	static bool timer_out_too_short;

	if (dump_log == DEEPIDLE_LOG_FULL) {
		wr = __spm_output_wake_reason(wakesta, pcmdesc, false);
	} else if (dump_log == DEEPIDLE_LOG_REDUCED) {
		/* Determine print SPM log or not */
		dpidle_log_print_curr_time = idle_get_current_time_ms();

		if (wakesta->assert_pc != 0)
			log_print = true;
		else if ((dpidle_log_print_curr_time - dpidle_log_print_prev_time) >
			 DPIDLE_LOG_DISCARD_CRITERIA)
			log_print = true;

		if (wakesta->timer_out <= DPIDLE_LOG_PRINT_TIMEOUT_CRITERIA)
			timer_out_too_short = true;

		/* Print SPM log */
		if (log_print == true) {
			dpidle_dbg("dpidle_log_discard_cnt = %d, timer_out_too_short = %d\n",
						dpidle_log_discard_cnt,
						timer_out_too_short);
			wr = __spm_output_wake_reason(wakesta, pcmdesc, false);

			dpidle_log_print_prev_time = dpidle_log_print_curr_time;
			dpidle_log_discard_cnt = 0;
			timer_out_too_short = false;
		} else {
			dpidle_log_discard_cnt++;

			wr = WR_NONE;
		}
	}

	return wr;
}

static void spm_dpidle_pre_process(void)
{
	/* set PMIC WRAP table for deepidle power control */
	__spm_set_pmic_phase(PMIC_WRAP_PHASE_DEEPIDLE);
}

static void spm_dpidle_post_process(void)
{
	/* set PMIC WRAP table for normal power control */
/*	__spm_set_pmic_phase(PMIC_WRAP_PHASE_NORMAL); */
}

wake_reason_t spm_go_to_dpidle(u32 spm_flags, u32 spm_data, u32 dump_log)
{
	struct wake_status wakesta;
	unsigned long flags;
	struct mtk_irq_mask mask;
	struct irq_desc *desc = irq_to_desc(spm_irq_0);
	wake_reason_t wr = WR_NONE;
	struct pcm_desc *pcmdesc = __spm_dpidle.pcmdesc;
	struct pwr_ctrl *pwrctrl = __spm_dpidle.pwrctrl;

#if SPM_AEE_RR_REC
	aee_rr_rec_deepidle_val(1 << SPM_DEEPIDLE_ENTER);
#endif

	set_pwrctrl_pcm_flags(pwrctrl, spm_flags);

	spm_dpidle_before_wfi();

	lockdep_off();
	spin_lock_irqsave(&__spm_lock, flags);

	mt_irq_mask_all(&mask);
	if (desc)
		unmask_irq(desc);
	mt_cirq_clone_gic();
	mt_cirq_enable();

#if SPM_AEE_RR_REC
	aee_rr_rec_deepidle_val(aee_rr_curr_deepidle_val() | (1 << SPM_DEEPIDLE_ENTER_UART_SLEEP));
#endif

	__spm_reset_and_init_pcm(pcmdesc);

	__spm_kick_im_to_fetch(pcmdesc);

	__spm_init_pcm_register();

	__spm_init_event_vector(pcmdesc);

	__spm_set_power_control(pwrctrl);

	__spm_set_wakeup_event(pwrctrl);

	spm_dpidle_pre_process();

	__spm_kick_pcm_to_run(pwrctrl);

#if SPM_AEE_RR_REC
	aee_rr_rec_deepidle_val(aee_rr_curr_deepidle_val() | (1 << SPM_DEEPIDLE_ENTER_WFI));
#endif

#ifdef SPM_DEEPIDLE_PROFILE_TIME
	gpt_get_cnt(SPM_PROFILE_APXGPT, &dpidle_profile[1]);
#endif
	spm_trigger_wfi_for_dpidle(pwrctrl);

#ifdef SPM_DEEPIDLE_PROFILE_TIME
	gpt_get_cnt(SPM_PROFILE_APXGPT, &dpidle_profile[2]);
#endif

#if SPM_AEE_RR_REC
	aee_rr_rec_deepidle_val(aee_rr_curr_deepidle_val() | (1 << SPM_DEEPIDLE_LEAVE_WFI));
#endif

	spm_dpidle_post_process();

	__spm_get_wakeup_status(&wakesta);

	__spm_clean_after_wakeup();

#if SPM_AEE_RR_REC
	aee_rr_rec_deepidle_val(aee_rr_curr_deepidle_val() | (1 << SPM_DEEPIDLE_ENTER_UART_AWAKE));
#endif

	wr = spm_output_wake_reason(&wakesta, pcmdesc, dump_log);

	mt_cirq_flush();
	mt_cirq_disable();

	mt_irq_mask_restore(&mask);

	spin_unlock_irqrestore(&__spm_lock, flags);
	lockdep_on();
	spm_dpidle_after_wfi();

#if SPM_AEE_RR_REC
	aee_rr_rec_deepidle_val(0);
#endif
	return wr;
}

/*
 * cpu_pdn:
 *    true  = CPU dormant
 *    false = CPU standby
 * pwrlevel:
 *    0 = AXI is off
 *    1 = AXI is 26M
 * pwake_time:
 *    >= 0  = specific wakeup period
 */
wake_reason_t spm_go_to_sleep_dpidle(u32 spm_flags, u32 spm_data)
{
	u32 sec = 0;
/*	int wd_ret; */
	struct wake_status wakesta;
	unsigned long flags;
	struct mtk_irq_mask mask;
	struct irq_desc *desc = irq_to_desc(spm_irq_0);
/*	struct wd_api *wd_api; */
	static wake_reason_t last_wr = WR_NONE;
	struct pcm_desc *pcmdesc = __spm_dpidle.pcmdesc;
	struct pwr_ctrl *pwrctrl = __spm_dpidle.pwrctrl;

	set_pwrctrl_pcm_flags(pwrctrl, spm_flags);

#if SPM_PWAKE_EN
	sec = spm_get_wake_period(-1 /* FIXME */, last_wr);
#endif
	pwrctrl->timer_val = sec * 32768;

	pwrctrl->wake_src = spm_get_sleep_wakesrc();

#if 0
	wd_ret = get_wd_api(&wd_api);
	if (!wd_ret)
		wd_api->wd_suspend_notify();
#endif
	spin_lock_irqsave(&__spm_lock, flags);

	mt_irq_mask_all(&mask);
	if (desc)
		unmask_irq(desc);
	mt_cirq_clone_gic();
	mt_cirq_enable();

	/* set PMIC WRAP table for deepidle power control */
/*	mt_cpufreq_set_pmic_phase(PMIC_WRAP_PHASE_DEEPIDLE); */

	spm_crit2("sleep_deepidle, sec = %u, wakesrc = 0x%x [%u]\n",
		  sec, pwrctrl->wake_src, is_cpu_pdn(pwrctrl->pcm_flags));

	__spm_reset_and_init_pcm(pcmdesc);

	__spm_kick_im_to_fetch(pcmdesc);

	if (request_uart_to_sleep()) {
		last_wr = WR_UART_BUSY;
		goto RESTORE_IRQ;
	}

	__spm_init_pcm_register();

	__spm_init_event_vector(pcmdesc);

	__spm_set_power_control(pwrctrl);

	__spm_set_wakeup_event(pwrctrl);

	__spm_kick_pcm_to_run(pwrctrl);

	spm_dpidle_pre_process();

	spm_trigger_wfi_for_dpidle(pwrctrl);

	spm_dpidle_post_process();

	__spm_get_wakeup_status(&wakesta);

	__spm_clean_after_wakeup();

	request_uart_to_wakeup();

	last_wr = __spm_output_wake_reason(&wakesta, pcmdesc, true);

RESTORE_IRQ:

	/* set PMIC WRAP table for normal power control */
/*	mt_cpufreq_set_pmic_phase(PMIC_WRAP_PHASE_NORMAL); */

	mt_cirq_flush();
	mt_cirq_disable();

	mt_irq_mask_restore(&mask);
	spin_unlock_irqrestore(&__spm_lock, flags);
#if 0
	if (!wd_ret)
		wd_api->wd_resume_notify();
#endif
	return last_wr;
}

#if SPM_USE_TWAM_DEBUG
#define SPM_TWAM_MONITOR_TICK 333333


static void twam_handler(struct twam_sig *twamsig)
{
	spm_crit("sig_high = %u%%  %u%%  %u%%  %u%%, r13 = 0x%x\n",
		 get_percent(twamsig->sig0, SPM_TWAM_MONITOR_TICK),
		 get_percent(twamsig->sig1, SPM_TWAM_MONITOR_TICK),
		 get_percent(twamsig->sig2, SPM_TWAM_MONITOR_TICK),
		 get_percent(twamsig->sig3, SPM_TWAM_MONITOR_TICK), spm_read(SPM_PCM_REG13_DATA));
}
#endif

void spm_deepidle_init(void)
{
#if SPM_USE_TWAM_DEBUG
	unsigned long flags;
	struct twam_sig twamsig = {
		.sig0 = 26,	/* md1_srcclkena */
		.sig1 = 22,	/* md_apsrc_req_mux */
		.sig2 = 25,	/* md2_srcclkena */
		.sig3 = 21,	/* md2_apsrc_req_mux */
	};

	spm_twam_register_handler(twam_handler);
	spm_twam_enable_monitor(&twamsig, false, SPM_TWAM_MONITOR_TICK);
#endif
}

MODULE_DESCRIPTION("SPM-DPIdle Driver v0.1");
