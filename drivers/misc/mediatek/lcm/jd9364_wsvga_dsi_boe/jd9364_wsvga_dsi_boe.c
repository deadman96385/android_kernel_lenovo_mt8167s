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

#ifdef BUILD_LK
#include <string.h>
#include <platform/mt_gpio.h>
#include <platform/mt_pmic.h>
#include <platform/upmu_common.h>
#include <platform/mt_i2c.h>
#include <platform/mt_pmic.h>
#elif defined(BUILD_UBOOT)
#include <asm/arch/mt_gpio.h>
#else
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <asm-generic/gpio.h>
#include <linux/string.h>
#include <linux/gpio.h>
#include <linux/pinctrl/consumer.h>
#endif

#ifdef CONFIG_OF
#include <linux/regulator/consumer.h>
#endif

#include "lcm_drv.h"

#ifndef BUILD_LK
static unsigned int GPIO_LCD_RST;

static void lcm_request_gpio_control(struct device *dev)
{
	GPIO_LCD_RST = of_get_named_gpio(dev->of_node, "gpio_lcd_rst", 0);
	gpio_request(GPIO_LCD_RST, "GPIO_LCD_RST");
}

static struct regulator *lcm_vgp;
/* get LDO supply */
static int lcm_get_vgp_supply(struct device *dev)
{
	int ret;
	struct regulator *lcm_vgp_ldo;

	pr_notice("LCM: lcm_get_vgp_supply is going\n");

	lcm_vgp_ldo = devm_regulator_get(dev, "reg-lcm");
	if (IS_ERR(lcm_vgp_ldo)) {
		ret = PTR_ERR(lcm_vgp_ldo);
		pr_debug("failed to get reg-lcm LDO\n");
		return ret;
	}

	pr_notice("LCM: lcm get supply ok.\n");

	/* get current voltage settings */
	ret = regulator_get_voltage(lcm_vgp_ldo);
	pr_notice("lcm LDO voltage = %d in LK stage\n", ret);

	lcm_vgp = lcm_vgp_ldo;

	return ret;
}

static int lcm_vgp_supply_enable(void)
{
	int ret;
	unsigned int volt;

	pr_notice("[Kernel/LCM] %s enter\n", __func__);

	if (lcm_vgp == NULL)
		return 0;

	pr_notice("LCM: set regulator voltage lcm_vgp voltage to 3.3V\n");
	/* set voltage to 3.3V */
	ret = regulator_set_voltage(lcm_vgp, 3300000, 3300000);
	if (ret != 0) {
		pr_debug("LCM: lcm failed to set lcm_vgp voltage\n");
		return ret;
	}

	/* get voltage settings again */
	volt = regulator_get_voltage(lcm_vgp);
	if (volt == 3300000)
		pr_notice("LCM: check regulator voltage=3300000 pass!\n");
	else
		pr_debug("LCM: check regulator voltage=3300000 fail! (voltage: %d)\n", volt);

	ret = regulator_enable(lcm_vgp);
	if (ret != 0) {
		pr_debug("LCM: Failed to enable lcm_vgp\n");
		return ret;
	}

	return ret;
}

static int lcm_vgp_supply_disable(void)
{
	int ret = 0;
	unsigned int isenable;

	if (lcm_vgp == NULL)
		return 0;

	/* disable regulator */
	isenable = regulator_is_enabled(lcm_vgp);

	pr_notice("LCM: lcm query regulator enable status[%d]\n", isenable);

	if (isenable) {
		ret = regulator_disable(lcm_vgp);
		if (ret != 0) {
			pr_debug("LCM: lcm failed to disable lcm_vgp\n");
			return ret;
		}
		/* verify */
		isenable = regulator_is_enabled(lcm_vgp);
		if (!isenable)
			pr_debug("LCM: lcm regulator disable pass\n");
	}

	return ret;
}

static int lcm_driver_probe(struct device *dev, void const *data)
{
	pr_notice("LCM: lcm_driver_probe\n");

	lcm_request_gpio_control(dev);
	lcm_get_vgp_supply(dev);
	lcm_vgp_supply_enable();
	return 0;
}

static const struct of_device_id lcm_platform_of_match[] = {
	{
		.compatible = "jd9364,jd9364_wsvga_dsi_boe",
		.data = 0,
	}, {
		/* sentinel */
	}
};

MODULE_DEVICE_TABLE(of, platform_of_match);

static int lcm_platform_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;

	id = of_match_node(lcm_platform_of_match, pdev->dev.of_node);
	if (!id)
		return -ENODEV;

	return lcm_driver_probe(&pdev->dev, id->data);
}

static struct platform_driver lcm_driver = {
	.probe = lcm_platform_probe,
	.driver = {
		.name = "jd9364_wsvga_dsi_boe",
		.owner = THIS_MODULE,
		.of_match_table = lcm_platform_of_match,
	},
};

static int __init lcm_init(void)
{
	if (platform_driver_register(&lcm_driver)) {
		pr_err("LCM: failed to register this driver!\n");
		return -ENODEV;
	}

	return 0;
}

static void __exit lcm_exit(void)
{
	platform_driver_unregister(&lcm_driver);
}

late_initcall(lcm_init);
module_exit(lcm_exit);
MODULE_AUTHOR("mediatek");
MODULE_DESCRIPTION("LCM display subsystem driver");
MODULE_LICENSE("GPL");
#endif

/* ------------------------------------------------------------------- */
/* Local Constants */
/* ------------------------------------------------------------------- */
#define FRAME_WIDTH  (600)
#define FRAME_HEIGHT (1024)

#define GPIO_OUT_ONE  1
#define GPIO_OUT_ZERO 0

#define REGFLAG_DELAY        0xFC
#define REGFLAG_UDELAY       0xFFFB
#define REGFLAG_END_OF_TABLE 0xFD

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/* ------------------------------------------------------------------- */
/* Local Functions */
/* ------------------------------------------------------------------- */
static LCM_UTIL_FUNCS lcm_util = { 0 };

#define SET_RESET_PIN(v) (lcm_util.set_reset_pin((v)))
#define UDELAY(n) (lcm_util.udelay(n))
#define MDELAY(n) (lcm_util.mdelay(n))

#define dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update) \
	lcm_util.dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update)
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) \
	lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update) \
	lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd) lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums) \
	lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg(cmd) lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size) \
	lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

/* ------------------------------------------------------------------- */
/* Local Variables */
/* ------------------------------------------------------------------- */
static void lcm_set_gpio_output(unsigned int GPIO, unsigned int output)
{
	if (GPIO == 0xFFFFFFFF) {
		pr_debug("[Kernel/LCM] GPIO_LCM_RST =   0x%x\n", GPIO_LCD_RST);
		return;
	}
	gpio_direction_output(GPIO, 0);
	gpio_set_value(GPIO, output);
}

struct LCM_setting_table {
	unsigned char cmd;
	unsigned char count;
	unsigned char para_list[64];
};
static struct LCM_setting_table lcm_suspend_setting[] = {
	{0x28, 0, {} },
	{REGFLAG_DELAY, 20, {} },
	{0x10, 0, {} },
	{REGFLAG_DELAY, 120, {} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

static struct LCM_setting_table lcm_initialization_setting[] = {
#if 0
	{0xE0, 1, {0x00} },
	{0xE1, 1, {0x93} },
	{0xE2, 1, {0x65} },
	{0xE3, 1, {0xF8} },
	{0xE0, 1, {0x01} },
	{0x17, 1, {0x00} },
	{0x18, 1, {0xB0} },
	{0x19, 1, {0x01} },
	{0x1A, 1, {0x00} },
	{0x1B, 1, {0xB0} },
	{0x1C, 1, {0x01} },
	{0x1F, 1, {0x3E} },
	{0x20, 1, {0x2F} },
	{0x21, 1, {0x2F} },
	{0x22, 1, {0x0E} },
	{0x37, 1, {0x69} },
	{0x38, 1, {0x05} },
	{0x39, 1, {0x00} },
	{0x3A, 1, {0x01} },
	{0x3C, 1, {0x90} },
	{0x3D, 1, {0xFF} },
	{0x3E, 1, {0xFF} },
	{0x3F, 1, {0xFF} },
	{0x40, 1, {0x02} },
	{0x41, 1, {0x80} },
	{0x42, 1, {0x99} },
	{0x43, 1, {0x06} },
	{0x44, 1, {0x09} },
	{0x45, 1, {0x3C} },
	{0x4B, 1, {0x04} },
	{0x55, 1, {0x0F} },
	{0x56, 1, {0x01} },
	{0x57, 1, {0x89} },
	{0x58, 1, {0x0A} },
	{0x59, 1, {0x0A} },
	{0x5A, 1, {0x27} },
	{0x5B, 1, {0x15} },
	{0x5D, 1, {0x7C} },
	{0x5E, 1, {0x67} },
	{0x5F, 1, {0x58} },
	{0x60, 1, {0x4C} },
	{0x61, 1, {0x48} },
	{0x62, 1, {0x38} },
	{0x63, 1, {0x3C} },
	{0x64, 1, {0x24} },
	{0x65, 1, {0x3B} },
	{0x66, 1, {0x38} },
	{0x67, 1, {0x36} },
	{0x68, 1, {0x53} },
	{0x69, 1, {0x3F} },
	{0x6A, 1, {0x44} },
	{0x6B, 1, {0x35} },
	{0x6C, 1, {0x2E} },
	{0x6D, 1, {0x1F} },
	{0x6E, 1, {0x0C} },
	{0x6F, 1, {0x00} },
	{0x70, 1, {0x7C} },
	{0x71, 1, {0x67} },
	{0x72, 1, {0x58} },
	{0x73, 1, {0x4C} },
	{0x74, 1, {0x48} },
	{0x75, 1, {0x38} },
	{0x76, 1, {0x3C} },
	{0x77, 1, {0x24} },
	{0x78, 1, {0x3B} },
	{0x79, 1, {0x38} },
	{0x7A, 1, {0x36} },
	{0x7B, 1, {0x53} },
	{0x7C, 1, {0x3F} },
	{0x7D, 1, {0x44} },
	{0x7E, 1, {0x35} },
	{0x7F, 1, {0x2E} },
	{0x80, 1, {0x1F} },
	{0x81, 1, {0x0C} },
	{0x82, 1, {0x00} },
	{0xE0, 1, {0x02} },
	{0x00, 1, {0x45} },
	{0x01, 1, {0x45} },
	{0x02, 1, {0x47} },
	{0x03, 1, {0x47} },
	{0x04, 1, {0x41} },
	{0x05, 1, {0x41} },
	{0x06, 1, {0x1F} },
	{0x07, 1, {0x1F} },
	{0x08, 1, {0x1F} },
	{0x09, 1, {0x1F} },
	{0x0A, 1, {0x1F} },
	{0x0B, 1, {0x1F} },
	{0x0C, 1, {0x1F} },
	{0x0D, 1, {0x1D} },
	{0x0E, 1, {0x1D} },
	{0x0F, 1, {0x1D} },
	{0x10, 1, {0x1F} },
	{0x11, 1, {0x1F} },
	{0x12, 1, {0x1F} },
	{0x13, 1, {0x1F} },
	{0x14, 1, {0x1F} },
	{0x15, 1, {0x1F} },
	{0x16, 1, {0x44} },
	{0x17, 1, {0x44} },
	{0x18, 1, {0x46} },
	{0x19, 1, {0x46} },
	{0x1A, 1, {0x40} },
	{0x1B, 1, {0x40} },
	{0x1C, 1, {0x1F} },
	{0x1D, 1, {0x1F} },
	{0x1E, 1, {0x1F} },
	{0x1F, 1, {0x1F} },
	{0x20, 1, {0x1F} },
	{0x21, 1, {0x1F} },
	{0x22, 1, {0x1F} },
	{0x23, 1, {0x1D} },
	{0x24, 1, {0x1D} },
	{0x25, 1, {0x1D} },
	{0x26, 1, {0x1F} },
	{0x27, 1, {0x1F} },
	{0x28, 1, {0x1F} },
	{0x29, 1, {0x1F} },
	{0x2A, 1, {0x1F} },
	{0x2B, 1, {0x1F} },
	{0x58, 1, {0x40} },
	{0x59, 1, {0x00} },
	{0x5A, 1, {0x00} },
	{0x5B, 1, {0x10} },
	{0x5C, 1, {0x07} },
	{0x5D, 1, {0x20} },
	{0x5E, 1, {0x00} },
	{0x5F, 1, {0x00} },
	{0x60, 1, {0x00} },
	{0x61, 1, {0x00} },
	{0x62, 1, {0x00} },
	{0x63, 1, {0x7A} },
	{0x64, 1, {0x7A} },
	{0x65, 1, {0x00} },
	{0x66, 1, {0x00} },
	{0x67, 1, {0x32} },
	{0x68, 1, {0x08} },
	{0x69, 1, {0x7A} },
	{0x6A, 1, {0x7A} },
	{0x6B, 1, {0x00} },
	{0x6C, 1, {0x00} },
	{0x6D, 1, {0x04} },
	{0x6E, 1, {0x04} },
	{0x6F, 1, {0x89} },
	{0x70, 1, {0x00} },
	{0x71, 1, {0x00} },
	{0x72, 1, {0x06} },
	{0x73, 1, {0x7B} },
	{0x74, 1, {0x00} },
	{0x75, 1, {0x07} },
	{0x76, 1, {0x00} },
	{0x77, 1, {0x5D} },
	{0x78, 1, {0x17} },
	{0x79, 1, {0x1F} },
	{0x7A, 1, {0x00} },
	{0x7B, 1, {0x00} },
	{0x7C, 1, {0x00} },
	{0x7D, 1, {0x03} },
	{0x7E, 1, {0x7B} },
	{0xE0, 1, {0x03} },
	{0xAF, 1, {0x20} },
	{0xE0, 1, {0x04} },
	{0x09, 1, {0x11} },
	{0x0E, 1, {0x48} },
	{0x2B, 1, {0x2B} },
	{0x2E, 1, {0x44} },
	{0x41, 1, {0xFF} },
	{0xE0, 1, {0x05} },
	{0x13, 1, {0x1D} },
	{0xE0, 1, {0x00} },
	{0xE6, 1, {0x02} },
	{0xE7, 1, {0x0C} },
#endif

	{0xE0,1,{0x00}},
	{0xE1,1,{0x93}},
	{0xE2,1,{0x65}},
	{0xE3,1,{0xF8}},
	// Sleep Out
	{0x11, 0, {0x00} },
	{REGFLAG_DELAY, 120, {0} },

	{0xE0,1,{0x04}},
	{0x2D,1,{0x03}},
	{0xE0,1,{0x00}},
	{0x80,1,{0x03}},
	// Display ON
	{0x29, 0, {0x00} },
	{REGFLAG_DELAY, 20, {0} },
};

static void push_table(void *cmdq, struct LCM_setting_table *table,
	unsigned int count, unsigned char force_update)
{
	unsigned int i;
	unsigned cmd;

	for (i = 0; i < count; i++) {

		cmd = table[i].cmd;

		switch (cmd) {

		case REGFLAG_DELAY:
			if (table[i].count <= 10)
				MDELAY(table[i].count);
			else
				MDELAY(table[i].count);
			break;

		case REGFLAG_UDELAY:
			UDELAY(table[i].count);
			break;

		case REGFLAG_END_OF_TABLE:
			break;

		default:
			dsi_set_cmdq_V2(cmd, table[i].count, table[i].para_list, force_update);
		}
	}
}

static void lcm_init_lcm(void)
{
	pr_notice("[Kernel/LCM] jd9364: %s enter\n", __func__);

	lcm_set_gpio_output(GPIO_LCD_RST, GPIO_OUT_ZERO);
	MDELAY(20);
	lcm_set_gpio_output(GPIO_LCD_RST, GPIO_OUT_ONE);
	MDELAY(60);
	lcm_set_gpio_output(GPIO_LCD_RST, GPIO_OUT_ZERO);
	MDELAY(30);
	lcm_set_gpio_output(GPIO_LCD_RST, GPIO_OUT_ONE);

	MDELAY(200);
	push_table(NULL, lcm_initialization_setting,
		sizeof(lcm_initialization_setting) / sizeof(struct LCM_setting_table),
		1);
}
static void lcm_suspend_power(void)
{
	pr_notice("[Kernel/LCM] jd9364: %s enter\n", __func__);
	lcm_vgp_supply_disable();
	MDELAY(20);
}

static void lcm_resume_power(void)
{
	pr_notice("[Kernel/LCM] jd9364: %s enter\n", __func__);
	lcm_vgp_supply_enable();
	MDELAY(20);
}

/* ------------------------------------------------------------------- */
/* LCM Driver Implementations */
/* ------------------------------------------------------------------- */
static void lcm_set_util_funcs(const LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(LCM_UTIL_FUNCS));
}

static void lcm_get_params(LCM_PARAMS *params)
{
	memset(params, 0, sizeof(LCM_PARAMS));

	params->type   = LCM_TYPE_DSI;

	params->width  = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;
	params->density = 160;
	params->physical_width = 89;
	params->physical_height = 152;
	params->dsi.mode = BURST_VDO_MODE;

	/* Command mode setting */
	params->dsi.LANE_NUM				= LCM_FOUR_LANE;
	params->dsi.data_format.format		= LCM_DSI_FORMAT_RGB888;
	/* Highly depends on LCD driver capability. */
	params->dsi.packet_size             = 256;
	params->dsi.vertical_sync_active    = 2;
	params->dsi.vertical_backporch      = 8;
	params->dsi.vertical_frontporch     = 6;
	params->dsi.vertical_active_line    = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active	= 15;
	params->dsi.horizontal_backporch	= 70;
	params->dsi.horizontal_frontporch	= 90;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;

	params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;

	params->dsi.ssc_disable = 1;
	params->dsi.HS_PRPR = 3;
	params->dsi.HS_TRAIL = 4;
	params->dsi.pll_select = 0; /* 0: MIPI_PLL; 1: LVDS_PLL */
	params->dsi.PLL_CLOCK = 150; /* this value must in MTK suggested table */

	params->dsi.esd_check_enable = 1;
	params->dsi.customization_esd_check_enable = 1;
	params->dsi.lcm_esd_check_table[0].cmd = 0x0A;
	params->dsi.lcm_esd_check_table[0].count = 1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x9C;

	params->dsi.pll_select = 0;
	params->dsi.PLL_CLOCK = 150;
	params->dsi.cont_clock = 1;
}

static void lcm_init_power(void)
{
	lcm_resume_power();
}

static void lcm_suspend(void)
{
	pr_notice("[Kernel/LCM] jd9364: %s enter\n", __func__);

	push_table(NULL, lcm_suspend_setting,
		sizeof(lcm_suspend_setting) / sizeof(struct LCM_setting_table),
		1);

	lcm_set_gpio_output(GPIO_LCD_RST, GPIO_OUT_ZERO);
	MDELAY(20);
}
static void lcm_resume(void)
{
	pr_notice("[Kernel/LCM] jd9364: %s enter\n", __func__);

	lcm_init_lcm();
}

LCM_DRIVER jd9364_wsvga_dsi_boe_lcm_drv = {
	.name = "jd9364_wsvga_dsi_boe",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init = lcm_init_lcm,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
	.init_power = lcm_init_power,
	.resume_power = lcm_resume_power,
	.suspend_power = lcm_suspend_power,
};
