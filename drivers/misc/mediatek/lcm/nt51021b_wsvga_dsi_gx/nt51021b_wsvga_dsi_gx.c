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

#ifndef BUILD_LK
#include <linux/string.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of_gpio.h>
#include <asm-generic/gpio.h>

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#endif
#endif

#include "lcm_drv.h"

#ifndef BUILD_LK
static struct regulator *lcm_vgp;
static unsigned int GPIO_LCD_RST;
/* GPIO51: high id normal mode, low is BIST mode*/
static unsigned int GPIO_LCD_ID1;

static void lcm_request_gpio_control(struct device *dev)
{
	pr_notice("[Kernel/LCM] nt51021b: %s", __func__);

	GPIO_LCD_ID1 = of_get_named_gpio(dev->of_node, "gpio_lcd_id1", 0);
	gpio_request(GPIO_LCD_ID1, "GPIO_LCD_ID1");

	GPIO_LCD_RST = of_get_named_gpio(dev->of_node, "gpio_lcd_rst", 0);
	gpio_request(GPIO_LCD_RST, "GPIO_LCD_RST");
}

/* get LDO supply */
static int lcm_get_vgp_supply(struct device *dev)
{
	int ret;
	struct regulator *lcm_vgp_ldo;

	pr_notice("[Kernel/LCM] %s enter\n", __func__);

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
	pr_notice("[Kernel/LCM] nt51021b: %s", __func__);

	lcm_request_gpio_control(dev);
	lcm_get_vgp_supply(dev);
	if (is_lk_show_logo)
		lcm_vgp_supply_enable();

	return 0;
}

static const struct of_device_id lcm_platform_of_match[] = {
	{
		.compatible = "nt51021b,nt51021b_wsvga_dsi_gx",
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
		.name = "nt51021b_wsvga_dsi_gx",
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

static LCM_UTIL_FUNCS lcm_util = { 0 };

#define SET_RESET_PIN(v)    (lcm_util.set_reset_pin((v)))

#define UDELAY(n) (lcm_util.udelay(n))
#define MDELAY(n) (lcm_util.mdelay(n))

#define dsi_set_cmdq_V3(para_tbl, size, force_update) \
	lcm_util.dsi_set_cmdq_V3(para_tbl, size, force_update)
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
/* Local Functions */
/* ------------------------------------------------------------------- */
static void lcm_set_gpio_output(unsigned int GPIO, unsigned int output)
{
	gpio_direction_output(GPIO, output);
	gpio_set_value(GPIO, output);
}

static void init_lcm_registers(void)
{
	unsigned int data_array[16];

	data_array[0] = 0x00831500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x00841500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x00841500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x04851500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x08861500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x8E8C1500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x2BC51500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x2BC71500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x04851500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x08861500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0xAA831500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x11841500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x109c1500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x4BA91500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x00831500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x00841500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x5BFD1500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x14FA1500;
	dsi_set_cmdq(data_array, 1, 1);

	data_array[0] = 0x00110500;
	dsi_set_cmdq(data_array, 1, 1);
	MDELAY(120);

	data_array[0] = 0x00290500;
	dsi_set_cmdq(data_array, 1, 1);
	MDELAY(20);

}

static void suspend_lcm_registers(void)
{
	unsigned int data_array[16];

	data_array[0] = 0x00280500;
	dsi_set_cmdq(data_array, 1, 1);
	MDELAY(20);
	data_array[0] = 0x00100500;
	dsi_set_cmdq(data_array, 1, 1);
	MDELAY(120);
}

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

	params->dsi.mode = BURST_VDO_MODE;
	params->dsi.LANE_NUM = LCM_FOUR_LANE;
	params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;
	params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;

	params->dsi.word_count              = FRAME_WIDTH * 3;
	params->dsi.vertical_sync_active    = 2;
	params->dsi.vertical_backporch      = 8;
	params->dsi.vertical_frontporch     = 6;
	params->dsi.vertical_active_line    = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active  = 32;
	params->dsi.horizontal_backporch    = 93;
	params->dsi.horizontal_frontporch   = 93;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;


	params->dsi.ssc_disable = 1;
	params->dsi.pll_select  = 0;
	params->dsi.PLL_CLOCK   = 168;
	params->dsi.cont_clock  = 1;
}

static void lcm_suspend(void)
{
#ifndef BUILD_LK
	pr_notice("[Kernel/LCM] nt51021b: %s enter\n", __func__);

	suspend_lcm_registers();

	lcm_set_gpio_output(GPIO_LCD_RST, GPIO_OUT_ZERO);
	MDELAY(20);

	lcm_vgp_supply_disable();
	MDELAY(20);

	lcm_set_gpio_output(GPIO_LCD_ID1, GPIO_OUT_ZERO);
	MDELAY(20);

#endif
}

static void lcm_resume(void)
{
#ifndef BUILD_LK
	pr_notice("[Kernel/LCM] nt51021b: %s enter\n", __func__);

	lcm_set_gpio_output(GPIO_LCD_ID1, GPIO_OUT_ONE);
	MDELAY(20);

	lcm_vgp_supply_enable();
	MDELAY(20);

	lcm_set_gpio_output(GPIO_LCD_RST, GPIO_OUT_ONE);
	MDELAY(20);
	lcm_set_gpio_output(GPIO_LCD_RST, GPIO_OUT_ZERO);
	MDELAY(10);
	lcm_set_gpio_output(GPIO_LCD_RST, GPIO_OUT_ONE);
	MDELAY(20);

	init_lcm_registers();
#endif
}

static void lcm_init_lcm(void)
{
#ifndef BUILD_LK
	pr_notice("[Kernel/LCM] nt51021b: %s enter\n", __func__);
	lcm_resume();
#endif
}

LCM_DRIVER nt51021b_wsvga_dsi_gx_lcm_drv = {
	.name = "nt51021b_wsvga_dsi_gx",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init = lcm_init_lcm,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
};
