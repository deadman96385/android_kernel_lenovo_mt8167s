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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/gpio.h>
#include <linux/io.h>
#ifndef CONFIG_OF
#include <mach/irqs.h>
#endif
#if defined(CONFIG_MTK_LEGACY)
#include <mt-plat/mt_gpio.h>
#include <cust_gpio_usage.h>
#endif
#include "musb_core.h"
#include <linux/platform_device.h>
#include "musbhsdma.h"
#include <linux/switch.h>
#include "usb20.h"
#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
#include <mt-plat/diso.h>
#include <mach/mtk_diso.h>
#endif
#ifdef CONFIG_OF
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#endif

#include "../../usb_c/inc/typec.h"

#ifdef CONFIG_OF
static unsigned int iddig_pin;
static unsigned int iddig_pin_mode;
static unsigned int iddig_if_config = 1;
#if !defined(OTG_BOOST_BY_SWITCH_CHARGER)
static unsigned int drvvbus_pin;
static unsigned int drvvbus_pin_mode;
static unsigned int drvvbus_if_config = 1;
#endif
#endif

#if !defined(CONFIG_MTK_LEGACY)
struct pinctrl *pinctrl;
int ocpgpio;
struct pinctrl_state *pinctrl_iddig;
struct pinctrl_state *pinctrl_drvvbus;
struct pinctrl_state *pinctrl_drvvbus_low;
struct pinctrl_state *pinctrl_drvvbus_high;
#endif
static int usb_iddig_number;
static int usb_typea_support = 0;

static struct musb_fifo_cfg fifo_cfg_host[] = {
{ .hw_ep_num =  1, .style = MUSB_FIFO_TX,   .maxpacket = 512, .mode = MUSB_BUF_SINGLE},
{ .hw_ep_num =  1, .style = MUSB_FIFO_RX,   .maxpacket = 512, .mode = MUSB_BUF_SINGLE},
{ .hw_ep_num =  2, .style = MUSB_FIFO_TX,   .maxpacket = 512, .mode = MUSB_BUF_SINGLE},
{ .hw_ep_num =  2, .style = MUSB_FIFO_RX,   .maxpacket = 512, .mode = MUSB_BUF_SINGLE},
{ .hw_ep_num =  3, .style = MUSB_FIFO_TX,   .maxpacket = 512, .mode = MUSB_BUF_SINGLE},
{ .hw_ep_num =  3, .style = MUSB_FIFO_RX,   .maxpacket = 512, .mode = MUSB_BUF_SINGLE},
{ .hw_ep_num =  4, .style = MUSB_FIFO_TX,   .maxpacket = 512, .mode = MUSB_BUF_SINGLE},
{ .hw_ep_num =  4, .style = MUSB_FIFO_RX,   .maxpacket = 512, .mode = MUSB_BUF_SINGLE},
{ .hw_ep_num =  5, .style = MUSB_FIFO_TX,   .maxpacket = 512, .mode = MUSB_BUF_SINGLE},
{ .hw_ep_num =	5, .style = MUSB_FIFO_RX,   .maxpacket = 512, .mode = MUSB_BUF_SINGLE},
{ .hw_ep_num =  6, .style = MUSB_FIFO_TX,   .maxpacket = 512, .mode = MUSB_BUF_SINGLE},
{ .hw_ep_num =	6, .style = MUSB_FIFO_RX,   .maxpacket = 512, .mode = MUSB_BUF_SINGLE},
{ .hw_ep_num =	7, .style = MUSB_FIFO_TX,   .maxpacket = 512, .mode = MUSB_BUF_SINGLE},
{ .hw_ep_num =	7, .style = MUSB_FIFO_RX,   .maxpacket = 512, .mode = MUSB_BUF_SINGLE},
{ .hw_ep_num =	8, .style = MUSB_FIFO_TX,   .maxpacket = 512, .mode = MUSB_BUF_SINGLE},
{ .hw_ep_num =	8, .style = MUSB_FIFO_RX,   .maxpacket = 64,  .mode = MUSB_BUF_SINGLE},
};

int delay_time = 15;
module_param(delay_time, int, 0400);
int delay_time1 = 55;
module_param(delay_time1, int, 0400);

int mt_typec_enable(void)
{
#if defined(CONFIG_USB_C_SWITCH)
	if (usb_c_switch_enable)
		return typec_support();
	else
#endif
		return 0;
}

void mt_usb_set_vbus(struct musb *musb, int is_on)
{
	DBG(0, "mt65xx_usb20_vbus++,is_on=%d\r\n", is_on);

	if (!mtk_otg_enable)
		return;

#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
	if (is_on)
		set_diso_otg(true);
	else
		set_diso_otg(false);
#endif

#ifndef FPGA_PLATFORM
	if (is_on) {
		/* power on VBUS, implement later... */
#if defined OTG_BOOST_BY_SWITCH_CHARGER
		tbl_charger_otg_vbus((work_busy(&musb->id_pin_work.work) << 8) | 1);
#else
#ifdef CONFIG_OF
#if defined(CONFIG_MTK_LEGACY)
		mt_set_gpio_mode(drvvbus_pin, drvvbus_pin_mode);
		mt_set_gpio_out(drvvbus_pin, GPIO_OUT_ONE);
#else
		DBG(0, "****%s:%d Drive VBUS ON!!!!!\n", __func__, __LINE__);
		pinctrl_select_state(pinctrl, pinctrl_drvvbus_high);
		if (gpio_is_valid(ocpgpio))
			gpio_direction_output(ocpgpio, 1);
#endif
#else
		mt_set_gpio_mode(GPIO_OTG_DRVVBUS_PIN, GPIO_OTG_DRVVBUS_PIN_M_GPIO);
		mt_set_gpio_out(GPIO_OTG_DRVVBUS_PIN, GPIO_OUT_ONE);
#endif
#endif
	} else {
		/* power off VBUS, implement later... */
#if defined OTG_BOOST_BY_SWITCH_CHARGER
		tbl_charger_otg_vbus((work_busy(&musb->id_pin_work.work) << 8) | 0);
#else
#ifdef CONFIG_OF
#if defined(CONFIG_MTK_LEGACY)
		mt_set_gpio_mode(drvvbus_pin, drvvbus_pin_mode);
		mt_set_gpio_out(drvvbus_pin, GPIO_OUT_ZERO);
#else
		DBG(0, "****%s:%d Drive VBUS OFF!!!!!\n", __func__, __LINE__);
		pinctrl_select_state(pinctrl, pinctrl_drvvbus_low);
		if (gpio_is_valid(ocpgpio))
			gpio_direction_output(ocpgpio, 0);
#endif
#else
		mt_set_gpio_mode(GPIO_OTG_DRVVBUS_PIN, GPIO_OTG_DRVVBUS_PIN_M_GPIO);
		mt_set_gpio_out(GPIO_OTG_DRVVBUS_PIN, GPIO_OUT_ZERO);
#endif
#endif
	}
#endif
}

int mt_usb_get_vbus_status(struct musb *musb)
{
#if 1
	return true;
#else
	int	ret = 0;

	if ((musb_readb(musb->mregs, MUSB_DEVCTL) & MUSB_DEVCTL_VBUS) != MUSB_DEVCTL_VBUS) {
		ret = 1;
	else
		DBG(0, "VBUS error, devctl=%x, power=%d\n", musb_readb(musb->mregs, MUSB_DEVCTL), musb->power);
		/*printk("vbus ready = %d\n", ret);*/
	return ret;
#endif
}

void mt_usb_init_drvvbus(void)
{
#if (!(defined(SWITCH_CHARGER) || defined(FPGA_PLATFORM))) && !(defined(OTG_BOOST_BY_SWITCH_CHARGER))
#ifdef CONFIG_OF
#if defined(CONFIG_MTK_LEGACY)
	mt_set_gpio_mode(drvvbus_pin, drvvbus_pin_mode);/*should set GPIO2 as gpio mode.*/
	mt_set_gpio_dir(drvvbus_pin, GPIO_DIR_OUT);
	mt_get_gpio_pull_enable(drvvbus_pin);
	mt_set_gpio_pull_select(drvvbus_pin, GPIO_PULL_UP);
#else
	int ret = 0;

	if (!mtk_otg_enable)
		return;

	DBG(0, "****%s:%d Init Drive VBUS!!!!!\n", __func__, __LINE__);

	pinctrl_drvvbus = pinctrl_lookup_state(pinctrl, "drvvbus_init");
	if (IS_ERR(pinctrl_drvvbus)) {
		ret = PTR_ERR(pinctrl_drvvbus);
		dev_err(mtk_musb->controller, "Cannot find usb pinctrl drvvbus\n");
	}

	pinctrl_drvvbus_low = pinctrl_lookup_state(pinctrl, "drvvbus_low");
	if (IS_ERR(pinctrl_drvvbus_low)) {
		ret = PTR_ERR(pinctrl_drvvbus_low);
		dev_err(mtk_musb->controller, "Cannot find usb pinctrl drvvbus_low\n");
	}

	pinctrl_drvvbus_high = pinctrl_lookup_state(pinctrl, "drvvbus_high");
	if (IS_ERR(pinctrl_drvvbus_high)) {
		ret = PTR_ERR(pinctrl_drvvbus_high);
		dev_err(mtk_musb->controller, "Cannot find usb pinctrl drvvbus_high\n");
	}

	pinctrl_select_state(pinctrl, pinctrl_drvvbus);
	DBG(0, "****%s:%d end Init Drive VBUS KS!!!!!\n", __func__, __LINE__);
#endif
#else
	mt_set_gpio_mode(GPIO_OTG_DRVVBUS_PIN, GPIO_OTG_DRVVBUS_PIN_M_GPIO);/*should set GPIO2 as gpio mode.*/
	mt_set_gpio_dir(GPIO_OTG_DRVVBUS_PIN, GPIO_DIR_OUT);
	mt_get_gpio_pull_enable(GPIO_OTG_DRVVBUS_PIN);
	mt_set_gpio_pull_select(GPIO_OTG_DRVVBUS_PIN, GPIO_PULL_UP);
#endif
#endif
}

int sw_deboun_time = 400;
module_param(sw_deboun_time, int, 0400);
struct switch_dev otg_state;

static bool musb_is_host(void)
{
	u8 devctl = 0;
	int iddig_state = 1;
	bool usb_is_host = 0;

	DBG(0, "will mask PMIC charger detection\n");
#ifndef FPGA_PLATFORM
#ifdef CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT
#ifdef CONFIG_MTK_LOAD_SWITCH_FPF3040
	pmic_chrdet_int_en(0);
#endif
#endif
#endif

	musb_platform_enable(mtk_musb);

#ifdef ID_PIN_USE_EX_EINT
#if defined(CONFIG_MTK_LEGACY)
	iddig_state = mt_get_gpio_in(iddig_pin);
#else
	iddig_state = __gpio_get_value(iddig_pin);
#endif
	DBG(0, "iddig_state = %d\n", iddig_state);
#else
	iddig_state = 0;
	devctl = musb_readb(mtk_musb->mregs, MUSB_DEVCTL);
	DBG(0, "devctl = %x before end session\n", devctl);
	devctl &= ~MUSB_DEVCTL_SESSION;	/* this will cause A-device change back to B-device after A-cable plug out*/
	musb_writeb(mtk_musb->mregs, MUSB_DEVCTL, devctl);
	msleep(delay_time);

	devctl = musb_readb(mtk_musb->mregs, MUSB_DEVCTL);
	DBG(0, "devctl = %x before set session\n", devctl);

	devctl |= MUSB_DEVCTL_SESSION;
	musb_writeb(mtk_musb->mregs, MUSB_DEVCTL, devctl);
	msleep(delay_time1);
	devctl = musb_readb(mtk_musb->mregs, MUSB_DEVCTL);
	DBG(0, "devclt = %x\n", devctl);
#endif

	if (devctl & MUSB_DEVCTL_BDEVICE || iddig_state) {
		DBG(0, "will unmask PMIC charger detection\n");
#ifndef FPGA_PLATFORM
		pmic_chrdet_int_en(1);
#endif
		usb_is_host = false;
	} else {
		usb_is_host = true;
	}

	DBG(0, "usb_is_host = %d\n", usb_is_host);
	return usb_is_host;
}

void musb_session_restart(struct musb *musb)
{
	void __iomem	*mbase = musb->mregs;

	if (!mtk_otg_enable)
		return;

	musb_writeb(mbase, MUSB_DEVCTL, (musb_readb(mbase, MUSB_DEVCTL) & (~MUSB_DEVCTL_SESSION)));
	DBG(0, "[MUSB] stopped session for VBUSERROR interrupt\n");
	USBPHY_SET8(0x6d, 0x3c);
	USBPHY_SET8(0x6c, 0x10);
	USBPHY_CLR8(0x6c, 0x2c);
	DBG(0, "[MUSB] force PHY to idle, 0x6d=%x, 0x6c=%x\n", USBPHY_READ8(0x6d), USBPHY_READ8(0x6c));
	mdelay(5);
	USBPHY_CLR8(0x6d, 0x3c);
	USBPHY_CLR8(0x6c, 0x3c);
	DBG(0, "[MUSB] let PHY resample VBUS, 0x6d=%x, 0x6c=%x\n", USBPHY_READ8(0x6d), USBPHY_READ8(0x6c));
	musb_writeb(mbase, MUSB_DEVCTL, (musb_readb(mbase, MUSB_DEVCTL) | MUSB_DEVCTL_SESSION));
	DBG(0, "[MUSB] restart session\n");
}

void switch_int_to_device(struct musb *musb)
{
	if (!mtk_otg_enable)
		return;

#ifdef ID_PIN_USE_EX_EINT
#if defined(CONFIG_MTK_LEGACY)
	mt_eint_set_polarity(IDDIG_EINT_PIN, MT_EINT_POL_POS);
	mt_eint_unmask(IDDIG_EINT_PIN);
#else
	if (usb_typea_support)
		irq_set_irq_type(usb_iddig_number, IRQF_TRIGGER_LOW);
	else
		irq_set_irq_type(usb_iddig_number, IRQF_TRIGGER_HIGH);
	enable_irq(usb_iddig_number);
	irq_set_irq_wake(usb_iddig_number, 1);
	DBG(0, "enable iddig irq HIGH @lin %d\n", __LINE__);
#endif
#else
	musb_writel(musb->mregs, USB_L1INTP, 0);
	musb_writel(musb->mregs, USB_L1INTM, IDDIG_INT_STATUS|musb_readl(musb->mregs, USB_L1INTM));
#endif
	DBG(0, "switch_int_to_device is done\n");
}

void switch_int_to_host(struct musb *musb)
{
	if (!mtk_otg_enable)
		return;

#ifdef ID_PIN_USE_EX_EINT
#if defined(CONFIG_MTK_LEGACY)
	mt_eint_set_polarity(IDDIG_EINT_PIN, MT_EINT_POL_NEG);
	mt_eint_unmask(IDDIG_EINT_PIN);
#else
	if (usb_typea_support)
		irq_set_irq_type(usb_iddig_number, IRQF_TRIGGER_HIGH);
	else
		irq_set_irq_type(usb_iddig_number, IRQF_TRIGGER_LOW);
	enable_irq(usb_iddig_number);
	irq_set_irq_wake(usb_iddig_number, 1);
	DBG(0, "enable iddig irq LOW @lin %d\n", __LINE__);
#endif
#else
	musb_writel(musb->mregs, USB_L1INTP, IDDIG_INT_STATUS);
	musb_writel(musb->mregs, USB_L1INTM, IDDIG_INT_STATUS|musb_readl(musb->mregs, USB_L1INTM));
#endif
	DBG(0, "switch_int_to_host is done\n");
}

void switch_int_to_host_and_mask(struct musb *musb)
{
	if (!mtk_otg_enable)
		return;

#ifdef ID_PIN_USE_EX_EINT
#if defined(CONFIG_MTK_LEGACY)
	mt_eint_set_polarity(IDDIG_EINT_PIN, MT_EINT_POL_NEG);
	mt_eint_mask(IDDIG_EINT_PIN);
#else
	irq_set_irq_wake(usb_iddig_number, 0);
	if (usb_typea_support)
		irq_set_irq_type(usb_iddig_number, IRQF_TRIGGER_HIGH);
	else
		irq_set_irq_type(usb_iddig_number, IRQF_TRIGGER_LOW);
	disable_irq(usb_iddig_number);
	DBG(0, "disable iddig irq @lin %d\n", __LINE__);
#endif
#else
	musb_writel(musb->mregs, USB_L1INTM, (~IDDIG_INT_STATUS)&musb_readl(musb->mregs, USB_L1INTM));
	mb(); /* */
	musb_writel(musb->mregs, USB_L1INTP, IDDIG_INT_STATUS);
#endif
	DBG(0, "swtich_int_to_host_and_mask is done\n");
}

static int typec_host_enable(void *data)
{
	u8 devctl = 0;
	unsigned long flags;

	mt_usb_clock_prepare(mtk_musb);

	spin_lock_irqsave(&mtk_musb->lock, flags);
	musb_generic_disable(mtk_musb);
	spin_unlock_irqrestore(&mtk_musb->lock, flags);

	down(&mtk_musb->musb_lock);
	DBG(0, "work start, is_host=%d\n", mtk_musb->is_host);
	if (mtk_musb->in_ipo_off) {
		DBG(0, "do nothing due to in_ipo_off\n");
		goto out;
	}

	mtk_musb->is_host = true;
	if (!mtk_musb->is_ready)
		mdelay(5000);
	musb_platform_enable(mtk_musb);
	DBG(0, "musb is as %s\n", mtk_musb->is_host?"host":"device");

	if (mtk_musb->is_host) {
		/*setup fifo for host mode*/
		ep_config_from_table_for_host(mtk_musb);
		wake_lock(&mtk_musb->usb_lock);
		musb_platform_set_vbus(mtk_musb, 1);

		/* for no VBUS sensing IP*/
#if 1
		/* wait VBUS ready */
		msleep(100);
		/* clear session*/
		devctl = musb_readb(mtk_musb->mregs, MUSB_DEVCTL);
		musb_writeb(mtk_musb->mregs, MUSB_DEVCTL, (devctl&(~MUSB_DEVCTL_SESSION)));
		/* USB MAC OFF*/
		/* VBUSVALID=0, AVALID=0, BVALID=0, SESSEND=1, IDDIG=X */
		USBPHY_SET8(0x6c, 0x10);
		USBPHY_CLR8(0x6c, 0x2e);
		USBPHY_SET8(0x6d, 0x3e);
		DBG(0, "force PHY to idle, 0x6d=%x, 0x6c=%x\n", USBPHY_READ8(0x6d), USBPHY_READ8(0x6c));
		/* wait */
		mdelay(5);

		/* remove babble: NOISE_STILL_SOF:1, BABBLE_CLR_EN:0 */
		devctl = musb_readb(mtk_musb->mregs, MUSB_ULPI_REG_DATA);
		devctl = devctl | 0x80;
		devctl = devctl & 0xbf;
		musb_writeb(mtk_musb->mregs, MUSB_ULPI_REG_DATA, devctl);
		mdelay(5);

		/* restart session */
		devctl = musb_readb(mtk_musb->mregs, MUSB_DEVCTL);
		musb_writeb(mtk_musb->mregs, MUSB_DEVCTL, (devctl | MUSB_DEVCTL_SESSION));
		/* USB MAC ONand Host Mode*/
		/* VBUSVALID=1, AVALID=1, BVALID=1, SESSEND=0, IDDIG=0 */
		USBPHY_CLR8(0x6c, 0x10);
		USBPHY_SET8(0x6c, 0x2c);
		USBPHY_SET8(0x6d, 0x3e);
		DBG(0, "force PHY to host mode, 0x6d=%x, 0x6c=%x\n", USBPHY_READ8(0x6d), USBPHY_READ8(0x6c));
#endif

		musb_start(mtk_musb);
		MUSB_HST_MODE(mtk_musb);

#ifdef CONFIG_PM_RUNTIME
		mtk_musb->is_active = 0;
		DBG(0, "set active to 0 in Pm runtime issue\n");
#endif
	}
	out:
		DBG(0, "work end, is_host=%d\n", mtk_musb->is_host);
		up(&mtk_musb->musb_lock);

	mt_usb_clock_unprepare(mtk_musb);

	return 0;
}



static int typec_host_disable(void *data)
{
	unsigned long flags;

	spin_lock_irqsave(&mtk_musb->lock, flags);
	musb_generic_disable(mtk_musb);
	spin_unlock_irqrestore(&mtk_musb->lock, flags);

	down(&mtk_musb->musb_lock);
	DBG(0, "work start, is_host=%d\n", mtk_musb->is_host);
	if (mtk_musb->in_ipo_off) {
		DBG(0, "do nothing due to in_ipo_off\n");
		goto out;
	}

	mtk_musb->is_host = false;
	musb_platform_enable(mtk_musb);
	DBG(0, "musb is as %s\n", mtk_musb->is_host?"host":"device");

	if (!mtk_musb->is_host) {
		DBG(0, "devctl is %x\n", musb_readb(mtk_musb->mregs, MUSB_DEVCTL));
		musb_writeb(mtk_musb->mregs, MUSB_DEVCTL, 0);
		if (wake_lock_active(&mtk_musb->usb_lock))
			wake_unlock(&mtk_musb->usb_lock);
		musb_platform_set_vbus(mtk_musb, 0);

	/* for no VBUS sensing IP */
#if 1
		/* USB MAC OFF*/
		/* VBUSVALID=0, AVALID=0, BVALID=0, SESSEND=1, IDDIG=X */
		USBPHY_SET8(0x6c, 0x10);
		USBPHY_CLR8(0x6c, 0x2e);
		USBPHY_SET8(0x6d, 0x3e);
		DBG(0, "force PHY to idle, 0x6d=%x, 0x6c=%x\n", USBPHY_READ8(0x6d), USBPHY_READ8(0x6c));
#endif

#if !defined(MTK_HDMI_SUPPORT)
		musb_stop(mtk_musb);
#else
		mt_usb_check_reconnect();/*ALPS01688604, IDDIG noise caused by MHL init*/
#endif
		mtk_musb->xceiv->otg->state = OTG_STATE_B_IDLE;

		MUSB_DEV_MODE(mtk_musb);
		}
	out:
		DBG(0, "work end, is_host=%d\n", mtk_musb->is_host);
		up(&mtk_musb->musb_lock);
	return 0;
}

struct typec_switch_data typec_host_driver = {
	.name = "mt_usb",
	.type = HOST_TYPE,
	.enable = typec_host_enable,
	.disable = typec_host_disable,
	.priv_data = NULL,
};

static int typec_switch_usb_connect(void *data)
{
	mt_usb_connect_c();
	DBG(0, "typec_switch_usb_connect \n");
	return 0;
}

static int typec_switch_usb_disconnect(void *data)
{
	mt_usb_disconnect_c();
	DBG(0, "typec_switch_usb_disconnect \n");
	return 0;
}

struct typec_switch_data typec_device_driver = {
	.name = (char*)musb_driver_name,
	.type = DEVICE_TYPE,
	.enable = typec_switch_usb_connect,
	.disable = typec_switch_usb_disconnect,
	.priv_data = NULL,
};


void mtk_typec_host_init(void)
{
	if (!mtk_otg_enable)
		return;

#if defined(CONFIG_USB_C_SWITCH)
	if (usb_c_switch_enable) {
		mtk_musb->fifo_cfg_host = fifo_cfg_host;
		mtk_musb->fifo_cfg_host_size = ARRAY_SIZE(fifo_cfg_host);
		otg_state.name = "otg_state";
		otg_state.index = 0;
		otg_state.state = 0;

		if (switch_dev_register(&otg_state))
			DBG(0, "switch_dev_register fail\n");
		else
			DBG(0, "switch_dev_register success\n");
		register_typec_switch_callback(&typec_host_driver);
		register_typec_switch_callback(&typec_device_driver);
	}
#endif

	return;
}


static void musb_id_pin_work(struct work_struct *data)
{
	u8 devctl = 0;
	unsigned long flags;

	/* need prepare clock because  musb_generic_disable may call prepare clock in atomic context */
	mt_usb_clock_prepare(mtk_musb);

	spin_lock_irqsave(&mtk_musb->lock, flags);
	musb_generic_disable(mtk_musb);
	spin_unlock_irqrestore(&mtk_musb->lock, flags);

	down(&mtk_musb->musb_lock);
	DBG(0, "work start, is_host=%d\n", mtk_musb->is_host);
	if (mtk_musb->in_ipo_off) {
		DBG(0, "do nothing due to in_ipo_off\n");
		goto out;
	}

	wake_lock(&mtk_musb->usb_lock);
	if (usb_typea_support)
		mtk_musb->is_host = !musb_is_host();
	else
		mtk_musb->is_host = musb_is_host();
	DBG(0, "musb is as %s, already lock\n", mtk_musb->is_host?"host":"device");
	switch_set_state((struct switch_dev *)&otg_state, mtk_musb->is_host);

	if (mtk_musb->is_host) {
		/*setup fifo for host mode*/
		ep_config_from_table_for_host(mtk_musb);
		/* wake_lock(&mtk_musb->usb_lock); */
		musb_platform_set_vbus(mtk_musb, 1);

		/* for no VBUS sensing IP*/
#if 1
		/* wait VBUS ready */
		msleep(100);
		/* clear session*/
		devctl = musb_readb(mtk_musb->mregs, MUSB_DEVCTL);
		musb_writeb(mtk_musb->mregs, MUSB_DEVCTL, (devctl&(~MUSB_DEVCTL_SESSION)));
		/* USB MAC OFF*/
		/* VBUSVALID=0, AVALID=0, BVALID=0, SESSEND=1, IDDIG=X */
		USBPHY_SET8(0x6c, 0x10);
		USBPHY_CLR8(0x6c, 0x2e);
		USBPHY_SET8(0x6d, 0x3e);
		DBG(0, "force PHY to idle, 0x6d=%x, 0x6c=%x\n", USBPHY_READ8(0x6d), USBPHY_READ8(0x6c));
		/* wait */
		mdelay(5);

		/* remove babble: NOISE_STILL_SOF:1, BABBLE_CLR_EN:0 */
		devctl = musb_readb(mtk_musb->mregs, MUSB_ULPI_REG_DATA);
		devctl = devctl | 0x80;
		devctl = devctl & 0xbf;
		musb_writeb(mtk_musb->mregs, MUSB_ULPI_REG_DATA, devctl);
		mdelay(5);

		/* restart session */
		devctl = musb_readb(mtk_musb->mregs, MUSB_DEVCTL);
		musb_writeb(mtk_musb->mregs, MUSB_DEVCTL, (devctl | MUSB_DEVCTL_SESSION));
		/* USB MAC ONand Host Mode*/
		/* VBUSVALID=1, AVALID=1, BVALID=1, SESSEND=0, IDDIG=0 */
		USBPHY_CLR8(0x6c, 0x10);
		USBPHY_SET8(0x6c, 0x2c);
		USBPHY_SET8(0x6d, 0x3e);
		DBG(0, "force PHY to host mode, 0x6d=%x, 0x6c=%x\n", USBPHY_READ8(0x6d), USBPHY_READ8(0x6c));
#endif

		musb_start(mtk_musb);
		MUSB_HST_MODE(mtk_musb);
		switch_int_to_device(mtk_musb);

#ifdef CONFIG_PM
		mtk_musb->is_active = 0;
		DBG(0, "set active to 0 in Pm runtime issue\n");
#endif
	} else {
		DBG(0, "devctl is %x\n", musb_readb(mtk_musb->mregs, MUSB_DEVCTL));
		musb_writeb(mtk_musb->mregs, MUSB_DEVCTL, 0);
		musb_platform_set_vbus(mtk_musb, 0);

	/* for no VBUS sensing IP */
#if 1
		/* USB MAC OFF*/
		/* VBUSVALID=0, AVALID=0, BVALID=0, SESSEND=1, IDDIG=X */
		USBPHY_SET8(0x6c, 0x10);
		USBPHY_CLR8(0x6c, 0x2e);
		USBPHY_SET8(0x6d, 0x3e);
		DBG(0, "force PHY to idle, 0x6d=%x, 0x6c=%x\n", USBPHY_READ8(0x6d), USBPHY_READ8(0x6c));
#endif

#if !defined(MTK_HDMI_SUPPORT)
		musb_stop(mtk_musb);
#else
		mt_usb_check_reconnect();/*ALPS01688604, IDDIG noise caused by MHL init*/
#endif

		if (wake_lock_active(&mtk_musb->usb_lock))
			wake_unlock(&mtk_musb->usb_lock);
		if (usb_typea_support)
			mt_usb_connect();
		mtk_musb->xceiv->otg->state = OTG_STATE_B_IDLE;
		MUSB_DEV_MODE(mtk_musb);
		switch_int_to_host(mtk_musb);
	}
out:
	DBG(0, "work end, is_host=%d\n", mtk_musb->is_host);
	up(&mtk_musb->musb_lock);

	mt_usb_clock_unprepare(mtk_musb);
}

#ifdef CONFIG_MTK_MUSB_SW_WITCH_MODE
void musb_id_pin_sw_work(bool host_mode)
{
	u8 devctl = 0;
	unsigned long flags;

	spin_lock_irqsave(&mtk_musb->lock, flags);
	musb_generic_disable(mtk_musb);
	spin_unlock_irqrestore(&mtk_musb->lock, flags);

	down(&mtk_musb->musb_lock);
	DBG(0, "work start, is_host=%d\n", mtk_musb->is_host);
	if (mtk_musb->in_ipo_off) {
		DBG(0, "do nothing due to in_ipo_off\n");
		goto out;
	}

	mtk_musb->is_host = host_mode;
	musb_platform_enable(mtk_musb);
	DBG(0, "musb is as %s\n", mtk_musb->is_host?"host":"device");
	switch_set_state((struct switch_dev *)&otg_state, mtk_musb->is_host);

	if (mtk_musb->is_host) {
		/*setup fifo for host mode*/
		ep_config_from_table_for_host(mtk_musb);
		wake_lock(&mtk_musb->usb_lock);
		musb_platform_set_vbus(mtk_musb, 1);

		/* for no VBUS sensing IP*/
#if 1
		/* wait VBUS ready */
		msleep(100);
		/* clear session*/
		devctl = musb_readb(mtk_musb->mregs, MUSB_DEVCTL);
		musb_writeb(mtk_musb->mregs, MUSB_DEVCTL, (devctl&(~MUSB_DEVCTL_SESSION)));
		/* USB MAC OFF*/
		/* VBUSVALID=0, AVALID=0, BVALID=0, SESSEND=1, IDDIG=X */
		USBPHY_SET8(0x6c, 0x10);
		USBPHY_CLR8(0x6c, 0x2e);
		USBPHY_SET8(0x6d, 0x3e);
		DBG(0, "force PHY to idle, 0x6d=%x, 0x6c=%x\n", USBPHY_READ8(0x6d), USBPHY_READ8(0x6c));
		/* wait */
		mdelay(5);

		/* remove babble: NOISE_STILL_SOF:1, BABBLE_CLR_EN:0 */
		devctl = musb_readb(mtk_musb->mregs, MUSB_ULPI_REG_DATA);
		devctl = devctl | 0x80;
		devctl = devctl & 0xbf;
		musb_writeb(mtk_musb->mregs, MUSB_ULPI_REG_DATA, devctl);
		mdelay(5);

		/* restart session */
		devctl = musb_readb(mtk_musb->mregs, MUSB_DEVCTL);
		musb_writeb(mtk_musb->mregs, MUSB_DEVCTL, (devctl | MUSB_DEVCTL_SESSION));
		/* USB MAC ONand Host Mode*/
		/* VBUSVALID=1, AVALID=1, BVALID=1, SESSEND=0, IDDIG=0 */
		USBPHY_CLR8(0x6c, 0x10);
		USBPHY_SET8(0x6c, 0x2c);
		USBPHY_SET8(0x6d, 0x3e);
		DBG(0, "force PHY to host mode, 0x6d=%x, 0x6c=%x\n", USBPHY_READ8(0x6d), USBPHY_READ8(0x6c));
#endif

		musb_start(mtk_musb);
		MUSB_HST_MODE(mtk_musb);
		switch_int_to_device(mtk_musb);

#ifdef CONFIG_PM
		mtk_musb->is_active = 0;
		DBG(0, "set active to 0 in Pm runtime issue\n");
#endif
	} else {
		DBG(0, "devctl is %x\n", musb_readb(mtk_musb->mregs, MUSB_DEVCTL));
		musb_writeb(mtk_musb->mregs, MUSB_DEVCTL, 0);
		if (wake_lock_active(&mtk_musb->usb_lock))
			wake_unlock(&mtk_musb->usb_lock);
		musb_platform_set_vbus(mtk_musb, 0);

	/* for no VBUS sensing IP */
#if 1
		/* USB MAC OFF*/
		/* VBUSVALID=0, AVALID=0, BVALID=0, SESSEND=1, IDDIG=X */
		USBPHY_SET8(0x6c, 0x10);
		USBPHY_CLR8(0x6c, 0x2e);
		USBPHY_SET8(0x6d, 0x3e);
		DBG(0, "force PHY to idle, 0x6d=%x, 0x6c=%x\n", USBPHY_READ8(0x6d), USBPHY_READ8(0x6c));
#endif

#if !defined(MTK_HDMI_SUPPORT)
		musb_stop(mtk_musb);
#else
		mt_usb_check_reconnect();/*ALPS01688604, IDDIG noise caused by MHL init*/
#endif
		mtk_musb->xceiv->otg->state = OTG_STATE_B_IDLE;

		MUSB_DEV_MODE(mtk_musb);
		switch_int_to_host(mtk_musb);
	}
out:
	DBG(0, "work end, is_host=%d\n", mtk_musb->is_host);
	up(&mtk_musb->musb_lock);
}
#endif


/*static void mt_usb_ext_iddig_int(void)*/
static irqreturn_t mt_usb_ext_iddig_int(int irq, void *dev_id)
{

#ifndef CONFIG_MTK_MUSB_SW_WITCH_MODE
	if (!mtk_musb->is_ready) {
		/* dealy 5 sec if usb function is not ready */
		schedule_delayed_work(&mtk_musb->id_pin_work, 7000*HZ/1000);
	} else {
		schedule_delayed_work(&mtk_musb->id_pin_work, sw_deboun_time*HZ/1000);
	}
#endif
	disable_irq_nosync(usb_iddig_number);
	DBG(0, "disable iddig irq @lin %d\n", __LINE__);
	DBG(0, "id pin interrupt assert\n");
	return IRQ_HANDLED;
}


void mt_usb_iddig_int(struct musb *musb)
{
	u32 usb_l1_ploy = musb_readl(musb->mregs, USB_L1INTP);

	if (!mtk_otg_enable)
		return;

	DBG(0, "id pin interrupt assert,polarity=0x%x\n", usb_l1_ploy);
	if (usb_l1_ploy & IDDIG_INT_STATUS)
		usb_l1_ploy &= (~IDDIG_INT_STATUS);
	else
		usb_l1_ploy |= IDDIG_INT_STATUS;

	musb_writel(musb->mregs, USB_L1INTP, usb_l1_ploy);
	musb_writel(musb->mregs, USB_L1INTM, (~IDDIG_INT_STATUS)&musb_readl(musb->mregs, USB_L1INTM));

	if (!mtk_musb->is_ready) {
		/* dealy 5 sec if usb function is not ready */
		schedule_delayed_work(&mtk_musb->id_pin_work, 5000*HZ/1000);
	} else {
		schedule_delayed_work(&mtk_musb->id_pin_work, sw_deboun_time*HZ/1000);
		DBG(0, "sw_deboun_time usb %d\n", sw_deboun_time);
	}
	DBG(0, "id pin mt_usb_iddig_int interrupt assert\n");
}


static void otg_int_init(void)
{

#if CONFIG_OF
#if defined(CONFIG_MTK_LEGACY)
	mt_set_gpio_mode(iddig_pin, GPIO_MODE_00);
	mt_set_gpio_dir(iddig_pin, GPIO_DIR_IN);
	mt_set_gpio_pull_enable(iddig_pin, GPIO_PULL_ENABLE);
	mt_set_gpio_pull_select(iddig_pin, GPIO_PULL_UP);

	mt_eint_set_sens(IDDIG_EINT_PIN, MT_LEVEL_SENSITIVE);
	mt_eint_set_hw_debounce(IDDIG_EINT_PIN, 64);
	mt_eint_registration(IDDIG_EINT_PIN, EINTF_TRIGGER_LOW, mt_usb_ext_iddig_int, FALSE);
#else
	int ret = 0;

	DBG(0, "****%s:%d before Init IDDIG KS!!!!!\n", __func__, __LINE__);

	pinctrl_iddig = pinctrl_lookup_state(pinctrl, "iddig_irq_init");
	if (IS_ERR(pinctrl_iddig)) {
		ret = PTR_ERR(pinctrl_iddig);
		DBG(0, "Cannot find usb pinctrl iddig_irq_init\n");
	} else {
		pinctrl_select_state(pinctrl, pinctrl_iddig);
	}
	DBG(0, "usb iddig_pin %d\n", iddig_pin);

#if 0
	gpio_set_debounce(iddig_pin, 64000);
	DBG(0, "will call __gpio_to_irq\n");
#endif
	usb_iddig_number = __gpio_to_irq(iddig_pin);
	DBG(0, "usb usb_iddig_number %d\n", usb_iddig_number);

	ret = request_irq(usb_iddig_number, mt_usb_ext_iddig_int, IRQF_TRIGGER_LOW, "USB_IDDIG", NULL);
	if (ret > 0)
		DBG(0, "USB IDDIG IRQ LINE not available!!\n");
	else
		DBG(0, "USB IDDIG IRQ LINE available!!\n");

	irq_set_irq_wake(usb_iddig_number, 1);
	if (usb_typea_support)
		schedule_delayed_work(&mtk_musb->id_pin_work, 400*HZ/1000);
#endif
#else
	u32 phy_id_pull = 0;

	phy_id_pull = __raw_readl(U2PHYDTM1);
	phy_id_pull |= ID_PULL_UP;
	__raw_writel(phy_id_pull, U2PHYDTM1);

	musb_writel(mtk_musb->mregs, USB_L1INTM, IDDIG_INT_STATUS | musb_readl(mtk_musb->mregs, USB_L1INTM));
#endif
}

void mt_usb_otg_init(struct musb *musb)
{
#ifdef CONFIG_OF
	struct device_node *node;
	struct property *prop;
	int ret;
#endif

	if (!mtk_otg_enable)
		return;

#if defined(CONFIG_MTK_DUAL_INPUT_CHARGER_SUPPORT)
#ifdef MTK_KERNEL_POWER_OFF_CHARGING
	if (g_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT || g_boot_mode == LOW_POWER_OFF_CHARGING_BOOT)
		return;
#endif
#endif

#ifdef CONFIG_OF

	node = of_find_compatible_node(NULL, NULL, "mediatek,mt8167-usb20");
	if (node == NULL) {
		DBG(0, "USB OTG - get node failed\n");
	} else {
		iddig_pin = of_get_named_gpio(node, "iddig_gpio", 0);
		if (iddig_pin == 0) {
			iddig_if_config = 0;
			DBG(0, "iddig_gpio fail\n");
		}
		iddig_pin_mode = of_get_named_gpio(node, "iddig_gpio", 1);

#if !defined(OTG_BOOST_BY_SWITCH_CHARGER)
		drvvbus_pin = of_get_named_gpio(node, "drvvbus_gpio", 0);
		if (drvvbus_pin == 0) {
			drvvbus_if_config = 0;
			DBG(0, "drvvbus_gpio fail\n");
		}
		drvvbus_pin_mode = of_get_named_gpio(node, "drvvbus_gpio", 1);
#endif
	prop = of_find_property(node, "type", NULL);
	if (prop && !strcmp(prop->name, "type")) {
		pr_err("find usb type node,%s\n",(char *)prop->value);
		if (!strcmp(prop->value, "a")) {
			usb_typea_support = 1;
			pr_err("usb_typea_support 1\n");
		} else {
			usb_typea_support = 0;
			pr_err("usb_typea_support 0\n");
		}
	}
	}
#if !defined(CONFIG_MTK_LEGACY)
	pinctrl = devm_pinctrl_get(mtk_musb->controller);
	if (IS_ERR(pinctrl))
		DBG(0, "Cannot find usb pinctrl!\n");
#endif
#endif
	DBG(0, "iddig_pin %x\n", iddig_pin);

#if !defined(OTG_BOOST_BY_SWITCH_CHARGER)
	DBG(0, "drvvbus_pin %x\n", drvvbus_pin);
	DBG(0, "drvvbus_pin_mode %d\n", drvvbus_pin_mode);
#endif
	ocpgpio = of_get_named_gpio(node, "ocp-gpio", 0);
	if (ocpgpio < 0)
		DBG(0, "Cannot find usb ocpgpio!\n");
	else {
		ret = gpio_request(ocpgpio, "ocp-gpio");
		if (ret)
			DBG(0, "gpio_request fail, ret(%d)\n", ret);
		else
			gpio_direction_output(ocpgpio, 1);
	}

	/*init drrvbus*/
	mt_usb_init_drvvbus();

	/* init idpin interrupt */
	INIT_DELAYED_WORK(&musb->id_pin_work, musb_id_pin_work);

	if (mt_typec_enable())
		return;

	otg_int_init();

	/* EP table */
	musb->fifo_cfg_host = fifo_cfg_host;
	musb->fifo_cfg_host_size = ARRAY_SIZE(fifo_cfg_host);

	otg_state.name = "otg_state";
	otg_state.index = 0;
	otg_state.state = 0;

	if (switch_dev_register(&otg_state))
		DBG(0, "switch_dev_register fail\n");
	else
		DBG(0, "switch_dev register success\n");

}

