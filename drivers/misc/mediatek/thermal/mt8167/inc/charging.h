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

/*****************************************************************************
 *
 * Filename:
 * ---------
 *    charging.h
 *
 * Project:
 * --------
 *   Maui_Software
 *
 * Description:
 * ------------
 *   This Module defines bmt internal charger hw setting function.
 *
 * Author:
 * -------
 *  Oscar Liu
 *
 *============================================================================
 *             HISTORY
 * Below this line, this part is controlled by PVCS VM. DO NOT MODIFY!!
 *------------------------------------------------------------------------------
 * Revision:   1.0
 * Modtime:   11 Aug 2005 10:28:16
 * Log:   //mtkvs01/vmdata/Maui_sw/archives/mcu/hal/peripheral/inc/bmt_chr_setting.h-arc
 *
 * 05 15 2015 wy.chuang
 * [ALPS01990538] [MP Feature Patch Back]MT6312 driver & MT6328 init setting & charging setting
 * .
 *
 * 03 04 2015 wy.chuang
 * [ALPS01921641] [L1_merge] for PMIC and charging
 * .
 *------------------------------------------------------------------------------
 * Upper this line, this part is controlled by PVCS VM. DO NOT MODIFY!!
 *============================================================================
 ****************************************************************************/
#ifndef CHARGING_H
#define CHARGING_H

#ifndef CONFIG_ARCH_MT8173
#include <mach/mtk_charging.h>
#endif

/* ============================================================ */
/* define */
/* ============================================================ */
/*****************************************************************************
 *  Log
 ****************************************************************************/
#define BAT_LOG_CRTI 1
#define BAT_LOG_FULL 2
#define BAT_LOG_DEBG 3

#define battery_xlog_printk(num, fmt, args...) \
do {\
	if (Enable_BATDRV_LOG >= (int)num) \
		pr_debug(fmt, ##args); \
} while (0)

#define battery_log(num, fmt, args...) \
do { \
	if (Enable_BATDRV_LOG >= (int)num) \
		switch (num) {\
		case BAT_LOG_CRTI:\
			pr_notice(fmt, ##args); \
			break; \
			/*fall-through*/\
		default: \
			pr_debug(fmt, ##args); \
			break; \
		} \
} while (0)

/* ============================================================ */
/* typedef */
/* ============================================================ */
typedef signed int(*CHARGING_CONTROL) (CHARGING_CTRL_CMD cmd, void *data);

#ifndef BATTERY_BOOL
#define BATTERY_BOOL
enum {
	KAL_FALSE = 0,
	KAL_TRUE  = 1,
};
#endif


/* ============================================================ */
/* External Variables */
/* ============================================================ */
extern int Enable_BATDRV_LOG;
extern kal_bool chargin_hw_init_done;
extern unsigned int g_bcct_flag;

/* ============================================================ */
/* External function */
/* ============================================================ */
extern signed int chr_control_interface(CHARGING_CTRL_CMD cmd, void *data);
extern unsigned int upmu_get_reg_value(unsigned int reg);
extern void Charger_Detect_Init(void);
extern void Charger_Detect_Release(void);
extern int hw_charging_get_charger_type(void);
extern void mt_power_off(void);

extern void hw_charging_enable_dp_voltage(int ison);


/* switch charger */

#if defined(CONFIG_MTK_SMART_BATTERY)
extern kal_bool pmic_chrdet_status(void);
#else
/*__weak kal_bool pmic_chrdet_status(void);*/
#endif
/*BCCT input current control function over switch charger*/
extern unsigned int set_chr_input_current_limit(int current_limit);

/* Set charger's boost current limit */
extern int set_chr_boost_current_limit(unsigned int current_limit);

/* Enable/Disable OTG mode */
extern int set_chr_enable_otg(unsigned int enable);

extern int mtk_chr_get_tchr(int *min_tchr, int *max_tchr);
extern int mtk_chr_get_soc(unsigned int *soc);
extern int mtk_chr_get_ui_soc(unsigned int *soc);
extern int mtk_chr_get_vbat(unsigned int *vbat);
extern int mtk_chr_get_ibat(unsigned int *ibat);
extern int mtk_chr_get_vbus(unsigned int *vbus);
extern int mtk_chr_get_aicr(unsigned int *aicr);
extern int mtk_chr_is_charger_exist(unsigned char *exist);
extern int mtk_chr_enable_direct_charge(unsigned char charging_enable);
extern int mtk_chr_enable_power_path(unsigned char en);
extern int mtk_chr_enable_charge(unsigned char charging_enable);
extern int mtk_chr_reset_aicr_upper_bound(void);

#endif /* #ifndef _CHARGING_H */
