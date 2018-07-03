/*
 * MUSB OTG driver core code
 *
 * Copyright 2005 Mentor Graphics Corporation
 * Copyright (C) 2005-2006 by Texas Instruments
 * Copyright (C) 2006-2007 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * Inventra (Multipoint) Dual-Role Controller Driver for Linux.
 *
 * This consists of a Host Controller Driver (HCD) and a peripheral
 * controller driver implementing the "Gadget" API; OTG support is
 * in the works.  These are normal Linux-USB controller drivers which
 * use IRQs and have no dedicated thread.
 *
 * This version of the driver has only been used with products from
 * Texas Instruments.  Those products integrate the Inventra logic
 * with other DMA, IRQ, and bus modules, as well as other logic that
 * needs to be reflected in this driver.
 *
 *
 * NOTE:  the original Mentor code here was pretty much a collection
 * of mechanisms that don't seem to have been fully integrated/working
 * for *any* Linux kernel version.  This version aims at Linux 2.6.now,
 * Key open issues include:
 *
 *  - Lack of host-side transaction scheduling, for all transfer types.
 *    The hardware doesn't do it; instead, software must.
 *
 *    This is not an issue for OTG devices that don't support external
 *    hubs, but for more "normal" USB hosts it's a user issue that the
 *    "multipoint" support doesn't scale in the expected ways.  That
 *    includes DaVinci EVM in a common non-OTG mode.
 *
 *      * Control and bulk use dedicated endpoints, and there's as
 *        yet no mechanism to either (a) reclaim the hardware when
 *        peripherals are NAKing, which gets complicated with bulk
 *        endpoints, or (b) use more than a single bulk endpoint in
 *        each direction.
 *
 *        RESULT:  one device may be perceived as blocking another one.
 *
 *      * Interrupt and isochronous will dynamically allocate endpoint
 *        hardware, but (a) there's no record keeping for bandwidth;
 *        (b) in the common case that few endpoints are available, there
 *        is no mechanism to reuse endpoints to talk to multiple devices.
 *
 *        RESULT:  At one extreme, bandwidth can be overcommitted in
 *        some hardware configurations, no faults will be reported.
 *        At the other extreme, the bandwidth capabilities which do
 *        exist tend to be severely undercommitted.  You can't yet hook
 *        up both a keyboard and a mouse to an external USB hub.
 */

/*
 * This gets many kinds of configuration information:
 *	- Kconfig for everything user-configurable
 *	- platform_device for addressing, irq, and platform_data
 *	- platform_data is mostly for board-specific informarion
 *	  (plus recentrly, SOC or family details)
 *
 * Most of the conditional compilation will (someday) vanish.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/prefetch.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/idr.h>
#include <linux/proc_fs.h>
#include <linux/wakelock.h>
#include <linux/ctype.h>
#include <linux/dma-mapping.h>
#ifdef CONFIG_OF
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#endif

#include "musbfsh_core.h"
#include "musbfsh_host.h"
#include "musbfsh_dma.h"
#include "musbfsh_hsdma.h"
#include "musbfsh_mt65xx.h"
#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
#include "musbfsh_qmu.h"
#include "mtk11_qmu.h"
u32 mtk11_dma_burst_setting, mtk11_qmu_ioc_setting;
/*struct musbfsh_hw_ep *mtk11_qmu_isoc_ep;*/
int mtk11_qmu_dbg_level = LOG_CRIT;
int mtk11_qmu_max_gpd_num;
int mtk11_isoc_ep_start_idx = 4;
int mtk11_isoc_ep_gpd_count = 3000;
int mtk11_host_qmu_concurrent = 1;
int mtk11_host_qmu_pipe_msk = (PIPE_ISOCHRONOUS + 1);/* | (PIPE_BULK + 1) | (PIPE_INTERRUPT+ 1) */;
int mtk11_host_qmu_max_active_isoc_gpd;
int mtk11_host_qmu_max_number_of_pkts;

module_param(mtk11_qmu_dbg_level, int, 0644);
module_param(mtk11_host_qmu_concurrent, int, 0644);
module_param(mtk11_host_qmu_pipe_msk, int, 0644);
module_param(mtk11_host_qmu_max_active_isoc_gpd, int, 0644);
module_param(mtk11_host_qmu_max_number_of_pkts, int, 0644);
#endif
#if (defined(CONFIG_MUSBFSH_VBUS) && !defined(CONFIG_MUSBFSH_IDDIG))
bool mtk11_suspend;
bool mtk11_with_device;
#endif
#ifdef CONFIG_MUSBFSH_VBUS
struct pinctrl *mt_pinctrl;
struct pinctrl_state *pinctrl_drvvbus1;
struct pinctrl_state *pinctrl_drvvbus1_low;
struct pinctrl_state *pinctrl_drvvbus1_high;
#endif
#ifdef CONFIG_MUSBFSH_IDDIG
struct pinctrl_state *pinctrl_usb11_iddig;
static int usb11_iddig_number;
static unsigned int usb11_iddig_pin;
struct wake_lock usb11_lock;
#endif

int musbfsh_host_dynamic_fifo = 1;
int musbfsh_host_dynamic_fifo_usage_msk;
module_param(musbfsh_host_dynamic_fifo, int, 0644);

#ifdef CONFIG_MUSBFSH_PIO_ONLY
#undef CONFIG_MUSBFSH_PIO_ONLY
#endif

#define DRIVER_AUTHOR "Mentor Graphics, Texas Instruments, Nokia, Mediatek"
#define DRIVER_DESC "MT65xx USB Host Controller Driver"

#define MUSBFSH_VERSION "6.0"

#define DRIVER_INFO DRIVER_DESC ", v" MUSBFSH_VERSION

#define MUSBFSH_DRIVER_NAME "musbfsh-hdrc"

u32 usb1_irq_number;
static const struct of_device_id apusb_of_ids[] = {
	{.compatible = "mediatek,mt8167-usb11",},
	{},
};

struct musbfsh *musbfsh_Device;
#ifdef CONFIG_OF
struct device_node *usb11_dts_np;
#endif

const char musbfsh_driver_name[] = MUSBFSH_DRIVER_NAME;
static DEFINE_IDA(musbfsh_ida);

MODULE_DESCRIPTION(DRIVER_INFO);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" MUSBFSH_DRIVER_NAME);

/*struct wake_lock musbfsh_suspend_lock;*/
DEFINE_SPINLOCK(musbfs_io_lock);
/*-------------------------------------------------------------------------*/
#ifdef IC_USB
static ssize_t show_start(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "start session under IC-USB mode\n");
}

static ssize_t store_start(struct device *dev, struct device_attribute *attr, const char *buf,
			   size_t size)
{
	char *pvalue = NULL;
	unsigned int value = 0;
	size_t count = 0;
	u8 devctl = musbfsh_readb((unsigned char __iomem *)USB11_BASE, MUSBFSH_DEVCTL);

	/*value = simple_strtoul(buf, &pvalue, 10);*/
	value = kstrtol(buf, 10, &pvalue); /* KS format requirement, sscanf -> kstrtol */
	count = pvalue - buf;

	if (*pvalue && isspace(*pvalue))
		count++;

	if (count == size) {
		if (value) {
			WARNING("[IC-USB]start session\n");
			devctl |= MUSBFSH_DEVCTL_SESSION;	/* wx? why not wait until device connected*/
			musbfsh_writeb((unsigned char __iomem *)USB11_BASE, MUSBFSH_DEVCTL, devctl);
			WARNING("[IC-USB]power on VSIM\n");
			hwPowerOn(MT65XX_POWER_LDO_VSIM, VOL_3000, "USB11-SIM");
		}
	}
	return size;
}

static DEVICE_ATTR(start, S_IWUSR | S_IWGRP | S_IRUGO, show_start, store_start);
#endif

/*-------------------------------------------------------------------------*/

static inline struct musbfsh *dev_to_musbfsh(struct device *dev)
{
	return hcd_to_musbfsh(dev_get_drvdata(dev));
}

/*-------------------------------------------------------------------------*/

int musbfsh_get_id(struct device *dev, gfp_t gfp_mask)
{
	int ret;
	int id;

	ret = ida_pre_get(&musbfsh_ida, gfp_mask);
	if (!ret) {
		dev_err(dev, "failed to reserve resource for id\n");
		return -ENOMEM;
	}

	ret = ida_get_new(&musbfsh_ida, &id);
	if (ret < 0) {
		dev_err(dev, "failed to allocate a new id\n");
		return ret;
	}

	return id;
}
EXPORT_SYMBOL_GPL(musbfsh_get_id);

void musbfsh_put_id(struct device *dev, int id)
{
	dev_dbg(dev, "removing id %d\n", id);
	ida_remove(&musbfsh_ida, id);
}
EXPORT_SYMBOL_GPL(musbfsh_put_id);

/*-------------------------------------------------------------------------*/
/*#ifdef CONFIG_MUSBFSH_PIO_ONLY*/
/*
* Load an endpoint's FIFO
*/
void musbfsh_write_fifo(struct musbfsh_hw_ep *hw_ep, u16 len, const u8 *src)
{
	void __iomem *fifo = hw_ep->fifo;

	prefetch((u8 *)src);

	INFO("%cX ep%d fifo %p count %d buf %p\n", 'T', hw_ep->epnum, fifo, len, src);
	INFO("[Flow][USB11]%s:%d\n", __func__, __LINE__);
	/* we can't assume unaligned reads work */
	if (likely((0x01 & (unsigned long)src) == 0)) {
		u16 index = 0;

		/* best case is 32bit-aligned source address */
		if ((0x02 & (unsigned long)src) == 0) {
			if (len >= 4) {
				/*writesl(fifo, src + index, len >> 2);*/
				iowrite32_rep(fifo, src + index, len >> 2);
				index += len & ~0x03;
			}
			if (len & 0x02) {
				musbfsh_writew(fifo, 0, *(u16 *)&src[index]);
				index += 2;
			}
		} else {
			if (len >= 2) {
				/*writesw(fifo, src + index, len >> 1);*/
				iowrite16_rep(fifo, src + index, len >> 1);
				index += len & ~0x01;
			}
		}
		if (len & 0x01)
			musbfsh_writeb(fifo, 0, src[index]);
	} else {
		/* byte aligned */
		/*writesb(fifo, src, len);*/
		iowrite8_rep(fifo, src, len);
	}
}

/*
 * Unload an endpoint's FIFO
*/
void musbfsh_read_fifo(struct musbfsh_hw_ep *hw_ep, u16 len, u8 *dst)
{
	void __iomem *fifo = hw_ep->fifo;

	INFO("[Flow][USB11]%s:%d\n", __func__, __LINE__);
	INFO("%cX ep%d fifo %p count %d buf %p\n", 'R', hw_ep->epnum, fifo, len, dst);

	/* we can't assume unaligned writes work */
	if (likely((0x01 & (unsigned long)dst) == 0)) {
		u16 index = 0;

		/* best case is 32bit-aligned destination address */
		if ((0x02 & (unsigned long)dst) == 0) {
			if (len >= 4) {
				/*readsl(fifo, dst, len >> 2);*/
				ioread32_rep(fifo, dst, len >> 2);
				index = len & ~0x03;
			}
			if (len & 0x02) {
				*(u16 *)&dst[index] = musbfsh_readw(fifo, 0);
				index += 2;
			}
		} else {
			if (len >= 2) {
				/*readsw(fifo, dst, len >> 1);*/
				ioread16_rep(fifo, dst, len >> 1);
				index = len & ~0x01;
			}
		}
		if (len & 0x01)
			dst[index] = musbfsh_readb(fifo, 0);
	} else {
		/* byte aligned */
		/*readsb(fifo, dst, len);*/
		ioread8_rep(fifo, dst, len);
	}
}


/*-------------------------------------------------------------------------*/

/* for high speed test mode; see USB 2.0 spec 7.1.20 */
static const u8 musbfsh_test_packet[53] = {
	/* implicit SYNC then DATA0 to start */

	/* JKJKJKJK x9 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* JJKKJJKK x8 */
	0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
	/* JJJJKKKK x8 */
	0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
	/* JJJJJJJKKKKKKK x8 */
	0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	/* JJJJJJJK x8 */
	0x7f, 0xbf, 0xdf, 0xef, 0xf7, 0xfb, 0xfd,
	/* JKKKKKKK x10, JK */
	0xfc, 0x7e, 0xbf, 0xdf, 0xef, 0xf7, 0xfb, 0xfd, 0x7e
	    /* implicit CRC16 then EOP to end */
};

void musbfsh_load_testpacket(struct musbfsh *musbfsh)
{
	void __iomem *regs = musbfsh->endpoints[0].regs;

	musbfsh_ep_select(musbfsh->mregs, 0);	/*should be implemented*/
	musbfsh_write_fifo(musbfsh->control_ep, sizeof(musbfsh_test_packet), musbfsh_test_packet);
	musbfsh_writew(regs, MUSBFSH_CSR0, MUSBFSH_CSR0_TXPKTRDY);
}

static struct musbfsh_fifo_cfg ep0_cfg = {
	.style = FIFO_RXTX, .maxpacket = 64,
};

/*-------------------------------------------------------------------------*/
/*
 * Interrupt Service Routine to record USB "global" interrupts.
 * Since these do not happen often and signify things of
 * paramount importance, it seems OK to check them individually;
 * the order of the tests is specified in the manual
 *
 * @param musb instance pointer
 * @param int_usb register contents
 * @param devctl
 * @param power
 */

static irqreturn_t musbfsh_stage0_irq(struct musbfsh *musbfsh, u8 int_usb, u8 devctl, u8 power)
{
	irqreturn_t handled = IRQ_NONE;

	/* in host mode, the peripheral may issue remote wakeup.
	 * in peripheral mode, the host may resume the link.
	 * spurious RESUME irqs happen too, paired with SUSPEND.
	 */
	if (int_usb & MUSBFSH_INTR_RESUME) {
		handled = IRQ_HANDLED;
		WARNING("RESUME!\n");

		if (devctl & MUSBFSH_DEVCTL_HM) {
			void __iomem *mbase = musbfsh->mregs;

			/* remote wakeup?  later, GetPortStatus
			 * will stop RESUME signaling
			 */

			if (power & MUSBFSH_POWER_SUSPENDM) {
				/* spurious */
				musbfsh->int_usb &= ~MUSBFSH_INTR_SUSPEND;
				WARNING("Spurious SUSPENDM\n");
			}

			power &= ~MUSBFSH_POWER_SUSPENDM;
			musbfsh_writeb(mbase, MUSBFSH_POWER, power | MUSBFSH_POWER_RESUME);

			musbfsh->port1_status |= (USB_PORT_STAT_C_SUSPEND << 16)
			    | MUSBFSH_PORT_STAT_RESUME;
			musbfsh->rh_timer = jiffies + msecs_to_jiffies(20);

			musbfsh->is_active = 1;
			usb_hcd_resume_root_hub(musbfsh_to_hcd(musbfsh));
		}
	}

	/* see manual for the order of the tests */
	if (int_usb & MUSBFSH_INTR_SESSREQ) {
		/*will not run to here */
		void __iomem *mbase = musbfsh->mregs;

		WARNING("SESSION_REQUEST\n");

		/* IRQ arrives from ID pin sense or (later, if VBUS power
		 * is removed) SRP.  responses are time critical:
		 *  - turn on VBUS (with silicon-specific mechanism)
		 *  - go through A_WAIT_VRISE
		 *  - ... to A_WAIT_BCON.
		 * a_wait_vrise_tmout triggers VBUS_ERROR transitions
		 */
		devctl |= MUSBFSH_DEVCTL_SESSION;
		musbfsh_writeb(mbase, MUSBFSH_DEVCTL, devctl);
		musbfsh->ep0_stage = MUSBFSH_EP0_START;
		musbfsh_platform_set_vbus(musbfsh, 1);

		handled = IRQ_HANDLED;
	}

	if (int_usb & MUSBFSH_INTR_VBUSERROR) {
		int ignore = 0;

		/* During connection as an A-Device, we may see a short
		 * current spikes causing voltage drop, because of cable
		 * and peripheral capacitance combined with vbus draw.
		 * (So: less common with truly self-powered devices, where
		 * vbus doesn't act like a power supply.)
		 *
		 * Such spikes are short; usually less than ~500 usec, max
		 * of ~2 msec.  That is, they're not sustained overcurrent
		 * errors, though they're reported using VBUSERROR irqs.
		 *
		 * Workarounds:  (a) hardware: use self powered devices.
		 * (b) software:  ignore non-repeated VBUS errors.
		 *
		 * REVISIT:  do delays from lots of DEBUG_KERNEL checks
		 * make trouble here, keeping VBUS < 4.4V ?
		 */
		if (musbfsh->vbuserr_retry) {
			void __iomem *mbase = musbfsh->mregs;

			musbfsh->vbuserr_retry--;
			ignore = 1;
			devctl |= MUSBFSH_DEVCTL_SESSION;
			musbfsh_writeb(mbase, MUSBFSH_DEVCTL, devctl);
		} else {
			musbfsh->port1_status |=
			    USB_PORT_STAT_OVERCURRENT | (USB_PORT_STAT_C_OVERCURRENT << 16);
		}

		ERR("VBUS_ERROR (%02x, %s), retry #%d, port1_status 0x%08x\n",
			devctl, ({
				char *s;

				switch (devctl & MUSBFSH_DEVCTL_VBUS) {
				case 0 << MUSBFSH_DEVCTL_VBUS_SHIFT:
					s = "<SessEnd"; break;
				case 1 << MUSBFSH_DEVCTL_VBUS_SHIFT:
					s = "<AValid"; break;
				case 2 << MUSBFSH_DEVCTL_VBUS_SHIFT:
					s = "<VBusValid"; break;
				/* case 3 << MUSBFSH_DEVCTL_VBUS_SHIFT: */
				default:
					s = "VALID"; break;
				}; s; }
		    ), VBUSERR_RETRY_COUNT - musbfsh->vbuserr_retry, musbfsh->port1_status);

		/* go through A_WAIT_VFALL then start a new session */
		if (!ignore)
			musbfsh_platform_set_vbus(musbfsh, 0);
		handled = IRQ_HANDLED;
	}

	if (int_usb & MUSBFSH_INTR_SUSPEND) {
		INFO("SUSPEND devctl %02x power %02x\n", devctl, power);
		handled = IRQ_HANDLED;
	}

	if (int_usb & MUSBFSH_INTR_CONNECT) {
		struct usb_hcd *hcd = musbfsh_to_hcd(musbfsh);

		handled = IRQ_HANDLED;
		musbfsh->is_active = 1;

		musbfsh->ep0_stage = MUSBFSH_EP0_START;
		musbfsh->port1_status &= ~(USB_PORT_STAT_LOW_SPEED
					   | USB_PORT_STAT_HIGH_SPEED | USB_PORT_STAT_ENABLE);
		musbfsh->port1_status |= USB_PORT_STAT_CONNECTION
		    | (USB_PORT_STAT_C_CONNECTION << 16);
		if (musbfsh_host_dynamic_fifo)
			musbfsh_host_dynamic_fifo_usage_msk = 0;

#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
		musbfsh_disable_q_all(musbfsh);
#endif

		/* high vs full speed is just a guess until after reset */
		if (devctl & MUSBFSH_DEVCTL_LSDEV)
			musbfsh->port1_status |= USB_PORT_STAT_LOW_SPEED;

		if (hcd->status_urb)
			usb_hcd_poll_rh_status(hcd);
		else
			usb_hcd_resume_root_hub(hcd);
#if (defined(CONFIG_MUSBFSH_VBUS) && !defined(CONFIG_MUSBFSH_IDDIG))
		mtk11_with_device = true;
#endif
		WARNING("CONNECT ! devctl 0x%02x\n", devctl);
	}

	if (int_usb & MUSBFSH_INTR_DISCONNECT) {
		WARNING("DISCONNECT !devctl %02x\n", devctl);
#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
		musbfsh_disable_q_all(musbfsh);
#endif

		handled = IRQ_HANDLED;
		usb_hcd_resume_root_hub(musbfsh_to_hcd(musbfsh));
		musbfsh_root_disconnect(musbfsh);
#if (defined(CONFIG_MUSBFSH_VBUS) && !defined(CONFIG_MUSBFSH_IDDIG))
		if (mtk11_suspend && mtk11_with_device)
			mt_usb11_clock_unprepare();
		else
			mtk11_with_device = false;
#endif
	}

	/* mentor saves a bit: bus reset and babble share the same irq.
	 * only host sees babble; only peripheral sees bus reset.
	 */
	if (int_usb & MUSBFSH_INTR_BABBLE) {
		handled = IRQ_HANDLED;
		/*
		 * Looks like non-HS BABBLE can be ignored, but
		 * HS BABBLE is an error condition. For HS the solution
		 * is to avoid babble in the first place and fix what
		 * caused BABBLE. When HS BABBLE happens we can only
		 * stop the session.
		 */
		if (devctl & (MUSBFSH_DEVCTL_FSDEV | MUSBFSH_DEVCTL_LSDEV)) {
			ERR("BABBLE devctl: %02x\n", devctl);
		} else {
			ERR("Stopping host session -- babble\n");
			devctl |= MUSBFSH_DEVCTL_SESSION;
			/*musbfsh_writeb(musbfsh->mregs, MUSBFSH_DEVCTL, devctl);*/ /*ignore babble*/
		}
	}

	return handled;
}

/*-------------------------------------------------------------------------*/
/*
* Program the HDRC to start (enable interrupts, dma, etc.).
*/
void musbfsh_start(struct musbfsh *musbfsh)
{
	void __iomem *regs = musbfsh->mregs;	/*base address of usb mac*/
	u8 devctl = musbfsh_readb(regs, MUSBFSH_DEVCTL);
	u8 power = musbfsh_readb(regs, MUSBFSH_POWER);
	int int_level1 = 0;

	WARNING("<== devctl 0x%02x\n", devctl);

	/*  Set INT enable registers, enable interrupts */
	musbfsh_writew(regs, MUSBFSH_INTRTXE, musbfsh->epmask);
	musbfsh_writew(regs, MUSBFSH_INTRRXE, musbfsh->epmask & 0xfffe);
	musbfsh_writeb(regs, MUSBFSH_INTRUSBE, 0xf7);
	/* enable level 1 interrupts */
	#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
	musbfsh_writew(regs, USB11_L1INTM, 0x002f);
	#else
	musbfsh_writew(regs, USB11_L1INTM, 0x000f);
	#endif
	int_level1 = musbfsh_readw(musbfsh->mregs, USB11_L1INTM);
	INFO("Level 1 Interrupt Mask 0x%x\n", int_level1);
	int_level1 = musbfsh_readw(musbfsh->mregs, USB11_L1INTP);
	INFO("Level 1 Interrupt Polarity 0x%x\n", int_level1);

	/* flush pending interrupts */
	musbfsh_writew(regs, MUSBFSH_INTRTX, 0xffff);
	musbfsh_writew(regs, MUSBFSH_INTRRX, 0xffff);
	musbfsh_writeb(regs, MUSBFSH_INTRUSB, 0xff);
	musbfsh_writeb(regs, MUSBFSH_HSDMA_INTR, 0xff);

	/*remove babble: NOISE_STALL_SOF:1, BABBLE_CLR_EN:0*/
	devctl = musbfsh_readb(regs, MUSBFSH_ULPI_REG_DATA);
	devctl = devctl | 0x80;
	devctl = devctl & 0xbf;
	musbfsh_writeb(regs, MUSBFSH_ULPI_REG_DATA, devctl);

	musbfsh->is_active = 0;
	musbfsh->is_multipoint = 1;

	/* need to enable the VBUS */
	musbfsh_platform_set_vbus(musbfsh, 1);
	musbfsh_platform_enable(musbfsh);

#ifndef IC_USB
	/* start session, assume ID pin is hard-wired to ground */
	devctl |= MUSBFSH_DEVCTL_SESSION;	/* wx? why not wait until device connected*/
	musbfsh_writeb(regs, MUSBFSH_DEVCTL, devctl);
#endif

	/* disable high speed negotiate */
	power |= MUSBFSH_POWER_HSENAB;
	power |= MUSBFSH_POWER_SOFTCONN;
	/*  enable SUSPENDM, this will put PHY into low power mode, not so "low" as save current mode,
	*  but it will be able to detect line state (remote wakeup/connect/disconnect)
	*/
	power |= MUSBFSH_POWER_ENSUSPEND;
	musbfsh_writeb(regs, MUSBFSH_POWER, power);

	devctl = musbfsh_readb(regs, MUSBFSH_DEVCTL);
	power = musbfsh_readb(regs, MUSBFSH_POWER);
	INFO(" musb ready. devctl=0x%x, power=0x%x\n", devctl, power);
	mdelay(500);		/* wx?*/
}


static void musbfsh_generic_disable(struct musbfsh *musbfsh)
{
	void __iomem *mbase = musbfsh->mregs;
	u16 temp;

	INFO("++\n");
	/* disable interrupts */
	musbfsh_writeb(mbase, MUSBFSH_INTRUSBE, 0);
	musbfsh_writew(mbase, MUSBFSH_INTRTXE, 0);
	musbfsh_writew(mbase, MUSBFSH_INTRRXE, 0);

	/* off */
	musbfsh_writeb(mbase, MUSBFSH_DEVCTL, 0);

	/*  flush pending interrupts */
	temp = musbfsh_readb(mbase, MUSBFSH_INTRUSB);
	temp = musbfsh_readw(mbase, MUSBFSH_INTRTX);
	temp = musbfsh_readw(mbase, MUSBFSH_INTRRX);
}

/*
 * Make the HDRC stop (disable interrupts, etc.);
 * reversible by musbfsh_start
 * called on gadget driver unregister
 * with controller locked, irqs blocked
 * acts as a NOP unless some role activated the hardware
 */
void musbfsh_stop(struct musbfsh *musbfsh)
{
	/* stop IRQs, timers, ... */
	musbfsh_platform_disable(musbfsh);
	musbfsh_generic_disable(musbfsh);
	INFO("HDRC disabled\n");

	/* FIXME
	 *  - mark host and/or peripheral drivers unusable/inactive
	 *  - disable DMA (and enable it in HdrcStart)
	 *  - make sure we can musbfsh_start() after musbfsh_stop(); with
	 *    OTG mode, gadget driver module rmmod/modprobe cycles that
	 *  - ...
	 */
	musbfsh_platform_try_idle(musbfsh, 0);
}

static void musbfsh_shutdown(struct platform_device *pdev)
{
	struct musbfsh *musbfsh = dev_to_musbfsh(&pdev->dev);
	unsigned long flags;

	WARNING("++\n");

	/* we prepare and enable usb clock first for avoiding schedule bug in stress tests
	 *  because  musbfsh_generic_disable may call clock prepare.
	 */
	mt_usb11_clock_prepare();
	mt65xx_usb11_clock_enable(true);
	musbfsh_power = true;

	spin_lock_irqsave(&musbfsh->lock, flags);
	musbfsh_platform_disable(musbfsh);
	musbfsh_generic_disable(musbfsh);
	musbfsh_platform_set_vbus(musbfsh, 0);
	musbfsh_platform_set_power(musbfsh, 0);
	spin_unlock_irqrestore(&musbfsh->lock, flags);

	mt65xx_usb11_clock_enable(false);
	mt_usb11_clock_unprepare();
	musbfsh_power = false;
	/* FIXME power down */
}


/*-------------------------------------------------------------------------*/
/*
 * tables defining fifo_mode values.  define more if you like.
 * for host side, make sure both halves of ep1 are set up.
 */

/* fits in 4KB */
#define MAXFIFOSIZE 8096

static struct musbfsh_fifo_cfg epx_cfg[] = {
	{.hw_ep_num = 1, .style = FIFO_TX, .maxpacket = 512, .mode = BUF_SINGLE},
	{.hw_ep_num = 1, .style = FIFO_RX, .maxpacket = 512, .mode = BUF_SINGLE},
	{.hw_ep_num = 2, .style = FIFO_TX, .maxpacket = 512, .mode = BUF_SINGLE},
	{.hw_ep_num = 2, .style = FIFO_RX, .maxpacket = 512, .mode = BUF_SINGLE},
	{.hw_ep_num = 3, .style = FIFO_TX, .maxpacket = 512, .mode = BUF_SINGLE},
	{.hw_ep_num = 3, .style = FIFO_RX, .maxpacket = 512, .mode = BUF_SINGLE},
	{.hw_ep_num = 4, .style = FIFO_TX, .maxpacket = 512, .mode = BUF_SINGLE},
	{.hw_ep_num = 4, .style = FIFO_RX, .maxpacket = 512, .mode = BUF_SINGLE},
	{.hw_ep_num = 5, .style = FIFO_TX, .maxpacket = 512, .mode = BUF_SINGLE},
	{.hw_ep_num = 5, .style = FIFO_RX, .maxpacket = 512, .mode = BUF_SINGLE},
	{.hw_ep_num = 6, .style = FIFO_TX, .maxpacket = 512, .mode = BUF_SINGLE},
	{.hw_ep_num = 6, .style = FIFO_RX, .maxpacket = 512, .mode = BUF_SINGLE},
	{.hw_ep_num = 7, .style = FIFO_TX, .maxpacket = 512, .mode = BUF_SINGLE},
	{.hw_ep_num = 7, .style = FIFO_RX, .maxpacket = 512, .mode = BUF_SINGLE},
};

/*-------------------------------------------------------------------------*/

/*
 * configure a fifo; for non-shared endpoints, this may be called
 * once for a tx fifo and once for an rx fifo.
 *
 * returns negative errno or offset for next fifo.
 */
static int
fifo_setup(struct musbfsh *musbfsh, struct musbfsh_hw_ep *hw_ep,
	   const struct musbfsh_fifo_cfg *cfg, u16 offset)
{
	void __iomem *mbase = musbfsh->mregs;
	int size = 0;
	u16 maxpacket = cfg->maxpacket;
	u16 c_off = offset >> 3;
	u8 c_size;		/*will be written into the fifo register*/

	INFO("hw_ep->epnum=%d,cfg->hw_ep_num=%d\n", hw_ep->epnum, cfg->hw_ep_num);
	/* expect hw_ep has already been zero-initialized */

	size = ffs(max_t(u16, maxpacket, 8)) - 1;
	maxpacket = 1 << size;

	c_size = size - 3;
	if (cfg->mode == BUF_DOUBLE) {
		if ((offset + (maxpacket << 1)) > MAXFIFOSIZE)
			return -EMSGSIZE;
		c_size |= MUSBFSH_FIFOSZ_DPB;
	} else {
		if ((offset + maxpacket) > MAXFIFOSIZE)
			return -EMSGSIZE;
	}

	/* configure the FIFO */
	musbfsh_writeb(mbase, MUSBFSH_INDEX, hw_ep->epnum);
	/* EP0 reserved endpoint for control, bidirectional;
	 * EP1 reserved for bulk, two unidirection halves.
	 */
	if (hw_ep->epnum == 1)
		musbfsh->bulk_ep = hw_ep;
	/* REVISIT error check:  be sure ep0 can both rx and tx ... */
	switch (cfg->style) {
	case FIFO_TX:
		musbfsh_write_txfifosz(mbase, c_size);
		musbfsh_write_txfifoadd(mbase, c_off);
		hw_ep->tx_double_buffered = !!(c_size & MUSBFSH_FIFOSZ_DPB);
		hw_ep->max_packet_sz_tx = maxpacket;
		break;
	case FIFO_RX:
		musbfsh_write_rxfifosz(mbase, c_size);
		musbfsh_write_rxfifoadd(mbase, c_off);
		hw_ep->rx_double_buffered = !!(c_size & MUSBFSH_FIFOSZ_DPB);
		hw_ep->max_packet_sz_rx = maxpacket;
		break;
	case FIFO_RXTX:
		musbfsh_write_txfifosz(mbase, c_size);
		musbfsh_write_txfifoadd(mbase, c_off);
		hw_ep->rx_double_buffered = !!(c_size & MUSBFSH_FIFOSZ_DPB);
		hw_ep->max_packet_sz_rx = maxpacket;

		musbfsh_write_rxfifosz(mbase, c_size);
		musbfsh_write_rxfifoadd(mbase, c_off);
		hw_ep->tx_double_buffered = hw_ep->rx_double_buffered;
		hw_ep->max_packet_sz_tx = maxpacket;
		hw_ep->is_shared_fifo = true;
		break;
	}

	/* NOTE rx and tx endpoint irqs aren't managed separately,
	 * which happens to be ok
	 */
	musbfsh->epmask |= (1 << hw_ep->epnum);

	return offset + (maxpacket << ((c_size & MUSBFSH_FIFOSZ_DPB) ? 1 : 0));
}

static int ep_config_from_table(struct musbfsh *musbfsh)
{
	const struct musbfsh_fifo_cfg *cfg = NULL;
	unsigned i = 0;
	unsigned n = 0;
	int offset;
	struct musbfsh_hw_ep *hw_ep = musbfsh->endpoints;

	if (musbfsh_host_dynamic_fifo)
		musbfsh_host_dynamic_fifo_usage_msk = 0;

	INFO("++\n");
	if (musbfsh->config->fifo_cfg) {
		cfg = musbfsh->config->fifo_cfg;
		n = musbfsh->config->fifo_cfg_size;
		INFO("fifo_cfg, n=%d\n", n);
		goto done;
	}

done:
	offset = fifo_setup(musbfsh, hw_ep, &ep0_cfg, 0);
	/* assert(offset > 0) */

	/* NOTE:  for RTL versions >= 1.400 EPINFO and RAMINFO would
	 * be better than static musbfsh->config->num_eps and DYN_FIFO_SIZE...
	 */

	for (i = 0; i < n; i++) {
		u8 epn = cfg->hw_ep_num;

		if (epn >= musbfsh->config->num_eps) {
			ERR("%s: invalid ep %d\n", musbfsh_driver_name, epn);
			return -EINVAL;
		}
		offset = fifo_setup(musbfsh, hw_ep + epn, cfg++, offset);
		if (offset < 0) {
			ERR("%s: mem overrun, ep %d\n", musbfsh_driver_name, epn);
			return -EINVAL;
		}

		epn++;		/*include ep0*/
		musbfsh->nr_endpoints = max(epn, musbfsh->nr_endpoints);
	}
	INFO("%s: %d/%d max ep, %d/%d memory\n",
	     musbfsh_driver_name, n + 1, musbfsh->config->num_eps * 2 - 1, offset, MAXFIFOSIZE);

	if (!musbfsh->bulk_ep) {
		ERR("%s: missing bulk\n", musbfsh_driver_name);
		return -EINVAL;
	}
	return 0;
}


/* Initialize MUSB (M)HDRC part of the USB hardware subsystem;
 * configure endpoints, or take their config from silicon
 */
static int musbfsh_core_init(struct musbfsh *musbfsh)
{
	void __iomem *mbase = musbfsh->mregs;
	int status = 0;
	int i;
	char aInfo[90];
	u8 reg;

	/* log core options (read using indexed model) */
	reg = musbfsh_read_configdata(mbase);
	strcpy(aInfo, (reg & MUSBFSH_CONFIGDATA_UTMIDW) ? "UTMI-16" : "UTMI-8");
	if (reg & MUSBFSH_CONFIGDATA_DYNFIFO) {
		strcat(aInfo, ", dyn FIFOs");
		musbfsh->dyn_fifo = true;
	}
	if (reg & MUSBFSH_CONFIGDATA_MPRXE) {
		strcat(aInfo, ", bulk combine");
		musbfsh->bulk_combine = true;
	}
	if (reg & MUSBFSH_CONFIGDATA_MPTXE) {
		strcat(aInfo, ", bulk split");
		musbfsh->bulk_split = true;
	}
	if (reg & MUSBFSH_CONFIGDATA_HBRXE) {
		strcat(aInfo, ", HB-ISO Rx");
		musbfsh->hb_iso_rx = true;
	}
	if (reg & MUSBFSH_CONFIGDATA_HBTXE) {
		strcat(aInfo, ", HB-ISO Tx");
		musbfsh->hb_iso_tx = true;
	}
	if (reg & MUSBFSH_CONFIGDATA_SOFTCONE)
		strcat(aInfo, ", SoftConn");

	WARNING("%s: ConfigData=0x%02x (%s)\n", musbfsh_driver_name, reg, aInfo);

	INFO("++\n");
	/* configure ep0 */
	musbfsh_configure_ep0(musbfsh);

	/* discover endpoint configuration */
	musbfsh->nr_endpoints = 1;	/*will update in func: ep_config_from_table*/
	musbfsh->epmask = 1;

	status = ep_config_from_table(musbfsh);
	if (status < 0)
		return status;

	/* finish init, and print endpoint config */
	for (i = 0; i < musbfsh->nr_endpoints; i++) {
		struct musbfsh_hw_ep *hw_ep = musbfsh->endpoints + i;

		hw_ep->fifo = MUSBFSH_FIFO_OFFSET(i) + mbase;
		hw_ep->regs = MUSBFSH_EP_OFFSET(i, 0) + mbase;
		hw_ep->rx_reinit = 1;
		hw_ep->tx_reinit = 1;

		if (hw_ep->max_packet_sz_tx) {
			INFO("%s: hw_ep %d%s, %smax %d,and hw_ep->epnum=%d\n",
			     musbfsh_driver_name, i,
			     hw_ep->is_shared_fifo ? "shared" : "tx",
			     hw_ep->tx_double_buffered
			     ? "doublebuffer, " : "", hw_ep->max_packet_sz_tx, hw_ep->epnum);
		}
		if (hw_ep->max_packet_sz_rx && !hw_ep->is_shared_fifo) {
			INFO("%s: hw_ep %d%s, %smax %d,and hw_ep->epnum=%d\n",
			     musbfsh_driver_name, i,
			     "rx",
			     hw_ep->rx_double_buffered
			     ? "doublebuffer, " : "", hw_ep->max_packet_sz_rx, hw_ep->epnum);
		}
		if (!(hw_ep->max_packet_sz_tx || hw_ep->max_packet_sz_rx))
			INFO("hw_ep %d not configured\n", i);
	}
	return 0;
}

/*-------------------------------------------------------------------------*/
void musbfsh_read_clear_generic_interrupt(struct musbfsh *musbfsh)
{
	musbfsh->int_usb = musbfsh_readb(musbfsh->mregs, MUSBFSH_INTRUSB);
	musbfsh->int_tx = musbfsh_readw(musbfsh->mregs, MUSBFSH_INTRTX);
	musbfsh->int_rx = musbfsh_readw(musbfsh->mregs, MUSBFSH_INTRRX);
	musbfsh->int_dma = musbfsh_readb(musbfsh->mregs, MUSBFSH_HSDMA_INTR);
	#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
	musbfsh->int_queue = musbfsh_readl(musbfsh->mregs, MUSBFSH_QISAR);
	#endif
	INFO("** musbfsh::IRQ! usb%04x tx%04x rx%04x dma%04x\n",
	     musbfsh->int_usb, musbfsh->int_tx, musbfsh->int_rx, musbfsh->int_dma);
	/* clear interrupt status */
	musbfsh_writew(musbfsh->mregs, MUSBFSH_INTRTX, musbfsh->int_tx);
	musbfsh_writew(musbfsh->mregs, MUSBFSH_INTRRX, musbfsh->int_rx);
	musbfsh_writeb(musbfsh->mregs, MUSBFSH_INTRUSB, musbfsh->int_usb);
	musbfsh_writeb(musbfsh->mregs, MUSBFSH_HSDMA_INTR, musbfsh->int_dma);
	#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
	musbfsh_writel(musbfsh->mregs, MUSBFSH_QISAR, musbfsh->int_queue);
	musbfsh->int_queue &= ~(musbfsh_readl(musbfsh->mregs, MUSBFSH_QIMR));
	#endif
}

static irqreturn_t generic_interrupt(int irq, void *__hci)
{
	unsigned long flags;
	irqreturn_t retval = IRQ_NONE;
	struct musbfsh *musbfsh = __hci;
	u16 int_level1 = 0;

	INFO("musbfsh:generic_interrupt++\r\n");
	spin_lock_irqsave(&musbfsh->lock, flags);

	musbfsh_read_clear_generic_interrupt(musbfsh);
	int_level1 = musbfsh_readw(musbfsh->mregs, USB11_L1INTS);
	INFO("Level 1 Interrupt Status 0x%x\r\n", int_level1);

#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
	if (musbfsh->int_usb || musbfsh->int_tx || musbfsh->int_rx || musbfsh->int_queue)
		retval = musbfsh_interrupt(musbfsh);
#else
	if (musbfsh->int_usb || musbfsh->int_tx || musbfsh->int_rx)
		retval = musbfsh_interrupt(musbfsh);
#endif

#ifndef CONFIG_MUSBFSH_PIO_ONLY
	if (musbfsh->int_dma)
		retval = musbfsh_dma_controller_irq(irq, musbfsh->musbfsh_dma_controller);
#endif

	spin_unlock_irqrestore(&musbfsh->lock, flags);
	return retval;
}

/*
 * handle all the irqs defined by the HDRC core. for now we expect:  other
 * irq sources (phy, dma, etc) will be handled first, musbfsh->int_* values
 * will be assigned, and the irq will already have been acked.
 *
 * called in irq context with spinlock held, irqs blocked
 */
irqreturn_t musbfsh_interrupt(struct musbfsh *musbfsh)
{
	irqreturn_t retval = IRQ_NONE;
	u8 devctl, power;
	int ep_num;
	u32 reg;

	devctl = musbfsh_readb(musbfsh->mregs, MUSBFSH_DEVCTL);
	power = musbfsh_readb(musbfsh->mregs, MUSBFSH_POWER);

	INFO("** musbfsh::devctl 0x%x power 0x%x\n", devctl, power);

	/* the core can interrupt us for multiple reasons; docs have
	 * a generic interrupt flowchart to follow
	 */
	if (musbfsh->int_usb)
		retval |= musbfsh_stage0_irq(musbfsh, musbfsh->int_usb, devctl, power);

	/* "stage 1" is handling endpoint irqs */

	/* handle endpoint 0 first */
	if (musbfsh->int_tx & 1)
		retval |= musbfsh_h_ep0_irq(musbfsh);

#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
	/* process generic queue interrupt */
	if (musbfsh->int_queue) {
		musbfsh_q_irq(musbfsh);
		retval = IRQ_HANDLED;
	}
#endif

	/* RX on endpoints 1-15 */
	reg = musbfsh->int_rx >> 1;
	ep_num = 1;
	while (reg) {
		if (reg & 1) {
			/* musbfsh_ep_select(musbfsh->mregs, ep_num); */
			/* REVISIT just retval = ep->rx_irq(...) */
			retval = IRQ_HANDLED;
			musbfsh_host_rx(musbfsh, ep_num);	/*the real ep_num*/
		}

		reg >>= 1;
		ep_num++;
	}

	/* TX on endpoints 1-15 */
	reg = musbfsh->int_tx >> 1;
	ep_num = 1;
	while (reg) {
		if (reg & 1) {
			/* musbfsh_ep_select(musbfsh->mregs, ep_num); */
			/* REVISIT just retval |= ep->tx_irq(...) */
			retval = IRQ_HANDLED;
			musbfsh_host_tx(musbfsh, ep_num);
		}
		reg >>= 1;
		ep_num++;
	}
	return retval;
}


#ifndef CONFIG_MUSBFSH_PIO_ONLY
static bool use_dma = 1;

/* "modprobe ... use_dma=0" etc */
module_param(use_dma, bool, 0);
MODULE_PARM_DESC(use_dma, "enable/disable use of DMA");

void musbfsh_dma_completion(struct musbfsh *musbfsh, u8 epnum, u8 transmit)
{
	INFO("++\n");
	/* called with controller lock already held */

	/* endpoints 1..15 */
	if (transmit)
		musbfsh_host_tx(musbfsh, epnum);
	else
		musbfsh_host_rx(musbfsh, epnum);
}

#else
#define use_dma			0
#endif

/* --------------------------------------------------------------------------
 * Init support
*/

static struct musbfsh *
allocate_instance(struct device *dev, struct musbfsh_hdrc_config *config, void __iomem *mbase)
{
	struct musbfsh *musbfsh;
	struct musbfsh_hw_ep *ep;
	int epnum;
	struct usb_hcd *hcd;

	INFO("++\n");

	musbfsh_hc_driver.flags = HCD_USB11 | HCD_MEMORY;
	hcd = usb_create_hcd(&musbfsh_hc_driver, dev, dev_name(dev));
	if (!hcd)
		return NULL;
	/* usbcore sets dev->driver_data to hcd, and sometimes uses that... */

	musbfsh = hcd_to_musbfsh(hcd);
	INIT_LIST_HEAD(&musbfsh->control);
	INIT_LIST_HEAD(&musbfsh->in_bulk);
	INIT_LIST_HEAD(&musbfsh->out_bulk);

	hcd->uses_new_polling = 1;
	hcd->has_tt = 1;

	musbfsh->vbuserr_retry = VBUSERR_RETRY_COUNT;

	musbfsh->mregs = mbase;
	musbfsh->ctrl_base = mbase;
	musbfsh->nIrq = -ENODEV;	/*will be update after return from this func*/
	musbfsh->config = config;
	if (musbfsh->config->num_eps > MUSBFSH_C_NUM_EPS)
		musbfsh_bug();

	for (epnum = 0, ep = musbfsh->endpoints; epnum < musbfsh->config->num_eps; epnum++, ep++) {
		ep->musbfsh = musbfsh;
		ep->epnum = epnum;
	}

	musbfsh->controller = dev;
	return musbfsh;
}

static void musbfsh_free(struct musbfsh *musbfsh)
{
	/* this has multiple entry modes. it handles fault cleanup after
	 * probe(), where things may be partially set up, as well as rmmod
	 * cleanup after everything's been de-activated.
	 */
	INFO("++\n");
	#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
	musbfsh_disable_q_all(musbfsh);
	musbfsh_qmu_exit(musbfsh);
	#endif

	if (musbfsh->nIrq >= 0) {
		if (musbfsh->irq_wake)
			disable_irq_wake(musbfsh->nIrq);
		free_irq(musbfsh->nIrq, musbfsh);
	}
	if (is_dma_capable() && musbfsh->dma_controller) {
		struct dma_controller *c = musbfsh->dma_controller;

		(void)c->stop(c);
		musbfsh_dma_controller_destroy(c);
	}
	usb_put_hcd(musbfsh_to_hcd(musbfsh));
	kfree(musbfsh);
}

/*
 * Perform generic per-controller initialization.
 *
 * @pDevice: the controller (already clocked, etc)
 * @nIrq: irq
 * @mregs: virtual address of controller registers,
 *	not yet corrected for platform-specific offsets
 */
#ifdef CONFIG_OF
#ifdef CONFIG_MUSBFSH_IDDIG
static void musbfsh_id_pin_work(struct work_struct *data)
{
	int iddig_state = 1;

	WARNING("musbfsh_id_pin_work\n");
	iddig_state = __gpio_get_value(usb11_iddig_pin);
	WARNING("iddig_state = %d\n", iddig_state);

#ifdef MUSBFSH_ENABLE_BUS_SUSPEND
	/* wake_lock(&usb11_lock);  */
#else
	wake_lock(&usb11_lock);
#endif

	if (!iddig_state) {
		WARNING("Device was plug in\n");
		pinctrl_select_state(mt_pinctrl, pinctrl_drvvbus1_high);
		mt_usb11_clock_prepare();
		mt65xx_usb11_clock_enable(true);
		musbfsh_power = true;
		WARNING("enable usb11 iddig irq HIGH @lin %d\n", __LINE__);
		irq_set_irq_type(usb11_iddig_number, IRQF_TRIGGER_HIGH);
		enable_irq(usb11_iddig_number);
		irq_set_irq_wake(usb11_iddig_number, 1);
	} else {
		WARNING("Device was unplug\n");
		pinctrl_select_state(mt_pinctrl, pinctrl_drvvbus1_low);
		mdelay(1000);
		WARNING("enable usb11 iddig irq LOW @lin %d\n", __LINE__);
		mt65xx_usb11_clock_enable(false);
		mt_usb11_clock_unprepare();
		musbfsh_power = false;
		irq_set_irq_type(usb11_iddig_number, IRQF_TRIGGER_LOW);
		enable_irq(usb11_iddig_number);
		irq_set_irq_wake(usb11_iddig_number, 1);
		if (wake_lock_active(&usb11_lock))
			wake_unlock(&usb11_lock);
	}
}

static irqreturn_t mt_usb11_iddig_int(int irq, void *dev_id)
{
	schedule_delayed_work(&musbfsh_Device->id_pin_work, 0);
	disable_irq_nosync(usb11_iddig_number);
	WARNING("disable iddig irq @lin %d\n", __LINE__);
	WARNING("id pin interrupt assert\n");
	return IRQ_HANDLED;
}

void musbfsh_iddig_int(void)
{
	int ret = 0;

	WARNING("****%s:%d before Init IDDIG KS!!!!!\n", __func__, __LINE__);
	pinctrl_usb11_iddig = pinctrl_lookup_state(mt_pinctrl, "iddig_irq");
	if (IS_ERR(pinctrl_usb11_iddig)) {
		ret = PTR_ERR(pinctrl_usb11_iddig);
		WARNING("Cannot find usb pinctrl iddig_irq\n");
	}
	pinctrl_select_state(mt_pinctrl, pinctrl_usb11_iddig);
	usb11_iddig_number = __gpio_to_irq(usb11_iddig_pin);
	WARNING("usb usb_iddig_number %d\n", usb11_iddig_number);

	ret = request_irq(usb11_iddig_number, mt_usb11_iddig_int, IRQF_TRIGGER_LOW, "USB11_IDDIG", NULL);
	if (ret > 0)
		WARNING("USB IDDIG IRQ LINE not available!!\n");
	else
		WARNING("USB IDDIG IRQ LINE available!!\n");
	irq_set_irq_wake(usb11_iddig_number, 1);
}
#endif

static int
musbfsh_init_controller(struct device *dev, int nIrq, void __iomem *ctrl, void __iomem *ctrlp)
#else
static int musbfsh_init_controller(struct device *dev, int nIrq, void __iomem *ctrl)
#endif
{
	int status;
	struct musbfsh *musbfsh;
	struct musbfsh_hdrc_platform_data *plat = dev->platform_data;
	struct usb_hcd *hcd;

	INFO("++\n");
	/* The driver might handle more features than the board; OK.
	 * Fail when the board needs a feature that's not enabled.
	 */
	INFO("[Flow][USB11]%s:%d,pbase= 0x%lx\n", __func__, __LINE__,
	       (unsigned long)ctrlp);
	if (!plat) {
		dev_dbg(dev, "no platform_data?\n");
		status = -ENODEV;
		goto fail0;
	}

	/* allocate */
	musbfsh = allocate_instance(dev, plat->config, ctrl);
	if (!musbfsh) {
		status = -ENOMEM;
		goto fail0;
	}

	spin_lock_init(&musbfsh->lock);
	musbfsh->board_mode = plat->mode;
	musbfsh->board_set_power = plat->set_power;
	musbfsh->ops = plat->platform_ops;

	musbfsh->config->fifo_cfg = epx_cfg;
	musbfsh->config->fifo_cfg_size = sizeof(epx_cfg) / sizeof(struct musbfsh_fifo_cfg);
	/* The musbfsh_platform_init() call:
	 *   - adjusts musbfsh->mregs and musbfsh->isr if needed,
	 *   - may initialize an integrated tranceiver
	 *   - initializes musbfsh->xceiv, usually by otg_get_transceiver()
	 *   - activates clocks.
	 *   - stops powering VBUS
	 *   - assigns musbfsh->board_set_vbus if host mode is enabled
	 *
	 * There are various transceiver configurations.  Blackfin,
	 * DaVinci, TUSB60x0, and others integrate them.  OMAP3 uses
	 * external/discrete ones in various flavors (twl4030 family,
	 * isp1504, non-OTG, etc) mostly hooking up through ULPI.
	 */
	musbfsh_Device = musbfsh;
#ifdef CONFIG_OF
	INFO("[Flow][USB11]%s:%d  unsigned longbase == 0x%lx ,musbfsh_Device->phy_reg_base = 0x%lx\n",
	     __func__, __LINE__, (unsigned long)ctrlp,
	     (unsigned long)(musbfsh_Device->phy_reg_base));

	musbfsh_Device->phy_reg_base = ctrlp;

#endif
	musbfsh->isr = generic_interrupt;
	INFO("[Flow][USB11]%s:%d  unsigned longbase == 0x%lx ,musbfsh_Device->phy_reg_base = 0x%lx\n",
	     __func__, __LINE__, (unsigned long)ctrlp,
	     (unsigned long)(musbfsh_Device->phy_reg_base));
	status = musbfsh_platform_init(musbfsh);
	INFO("[Flow][USB11]%s:%d\n", __func__, __LINE__);
	if (status < 0) {
		ERR("musbfsh_platform_init fail!status=%d", status);
		goto fail1;
	}
	INFO("[Flow][USB11]%s:%d\n", __func__, __LINE__);
	if (!musbfsh->isr) {
		status = -ENODEV;
		goto fail2;
	}
#ifndef CONFIG_MUSBFSH_PIO_ONLY
	INFO("DMA mode\n");
	INFO("[Flow][USB11]%s:%d  DMA Mode\n", __func__, __LINE__);
	if (use_dma && dev->dma_mask) {
		struct dma_controller *c;

		c = musbfsh_dma_controller_create(musbfsh, musbfsh->mregs);	/*only software config*/
		musbfsh->dma_controller = c;
		if (c)
			(void)c->start(c);	/*do nothing in fact*/
	}
#else
	INFO("PIO mode\n");
	INFO("[Flow][USB11]%s:%d	PIO Mode\n", __func__, __LINE__);
#endif

	/* ideally this would be abstracted in platform setup */
	if (!is_dma_capable() || !musbfsh->dma_controller)
		dev->dma_mask = NULL;

	/* be sure interrupts are disabled before connecting ISR */
	musbfsh_platform_disable(musbfsh);	/*wz,need implement in MT65xx, but not power off!*/
	musbfsh_generic_disable(musbfsh);	/*must power on the USB module*/

	/* setup musb parts of the core (especially endpoints) */
	status = musbfsh_core_init(musbfsh);
	if (status < 0) {
		ERR("musbfsh_core_init fail!");
		goto fail2;
	}

	#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
	musbfsh_qmu_init(musbfsh);
	#endif

	/* attach to the IRQ */
	INFO("[Flow][USB11]%s:%d ,request_irq %d\n", __func__, __LINE__, nIrq);
	/*wx? usb_add_hcd will also try do request_irq, if hcd_driver.irq is set*/
	if (request_irq(nIrq, musbfsh->isr, IRQF_TRIGGER_LOW, dev_name(dev), musbfsh)) {
		dev_err(dev, "musbfsh::request_irq %d failed!\n", nIrq);
		status = -ENODEV;
		goto fail3;
	}
	musbfsh->nIrq = nIrq;	/*update the musbfsh->nIrq after request_irq !*/
	/* FIXME this handles wakeup irqs wrong */
	if (enable_irq_wake(nIrq) == 0) {	/*wx, need to be replaced by modifying kernel/core/mt6573_ost.c*/
		musbfsh->irq_wake = 1;
		device_init_wakeup(dev, 1);	/*wx? usb_add_hcd will do this any way*/
	} else {
		musbfsh->irq_wake = 0;
	}

	/* host side needs more setup */
	hcd = musbfsh_to_hcd(musbfsh);
	/*plat->power can be set to 0,so the power is set to 500ma .*/
	hcd->power_budget = 2 * (plat->power ? plat->power : 250);

	/* For the host-only role, we can activate right away.
	 * (We expect the ID pin to be forcibly grounded!!)
	 * Otherwise, wait till the gadget driver hooks up.
	 */
	status = usb_add_hcd(musbfsh_to_hcd(musbfsh), -1, 0);	/*mportant!!*/
	hcd->self.uses_pio_for_control = 1;

	if (status < 0) {
		ERR("usb_add_hcd fail!");
		goto fail3;
	}

	/*initial done, turn off usb */
	#ifdef CONFIG_MUSBFSH_IDDIG
	mt65xx_usb11_clock_enable(false);
	mt_usb11_clock_unprepare();
	musbfsh_power = false;
	#endif

	status = musbfsh_init_debugfs(musbfsh);

	if (status < 0) {
		ERR("usb_add_debugfs fail!");
		goto fail4;
	}
	dev_info(dev, "USB controller at %p using %s, IRQ %d\n",
		 ctrl, (is_dma_capable() && musbfsh->dma_controller)
		 ? "DMA" : "PIO", musbfsh->nIrq);

	return 0;


fail4:
	musbfsh_exit_debugfs(musbfsh);

fail3:
	#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
	musbfsh_qmu_exit(musbfsh);
	#endif

fail2:
	if (musbfsh->irq_wake)
		device_init_wakeup(dev, 0);
	musbfsh_platform_exit(musbfsh);

fail1:
	dev_err(musbfsh->controller, "musbfsh_init_controller failed with status %d\n", status);
	musbfsh_free(musbfsh);

fail0:

	return status;

}

/*-------------------------------------------------------------------------*/

/* all implementations (PCI bridge to FPGA, VLYNQ, etc) should just
 * bridge to a platform device; this driver then suffices.
 */

#ifndef CONFIG_MUSBFSH_PIO_ONLY
static u64 *orig_dma_mask;
#endif

#ifdef CONFIG_MUSBFSH_VBUS
void musbfsh_init_drvvbus(void)
{
	int ret = 0;

	WARNING("****%s:%d Init Drive VBUS!!!!!\n", __func__, __LINE__);

	pinctrl_drvvbus1 = pinctrl_lookup_state(mt_pinctrl, "vbus_init");
	if (IS_ERR(pinctrl_drvvbus1)) {
		ret = PTR_ERR(pinctrl_drvvbus1);
		WARNING("ERROR:Cannot find usb pinctrl drvvbus1\n");
		return;
	}

	pinctrl_drvvbus1_low = pinctrl_lookup_state(mt_pinctrl, "vbus_low");
	if (IS_ERR(pinctrl_drvvbus1_low)) {
		ret = PTR_ERR(pinctrl_drvvbus1_low);
		WARNING("ERROR:Cannot find usb pinctrl drvvbus1_low\n");
		return;
	}

	pinctrl_drvvbus1_high = pinctrl_lookup_state(mt_pinctrl, "vbus_high");
	if (IS_ERR(pinctrl_drvvbus1_high)) {
		ret = PTR_ERR(pinctrl_drvvbus1_high);
		WARNING("ERROR:Cannot find usb pinctrl drvvbus1_high\n");
	}

	pinctrl_select_state(mt_pinctrl, pinctrl_drvvbus1);
	WARNING("****%s:%d end Init Drive VBUS KS!!!!!\n", __func__, __LINE__);

}
#endif

static int musbfsh_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node;
	#ifdef CONFIG_MUSBFSH_VBUS
	struct platform_device *pdev_node;
	#endif

	int irq = MT_USB1_IRQ_ID;
	int status;
	unsigned char __iomem *base = (unsigned char __iomem *)USB11_BASE;
#ifdef CONFIG_OF
	void __iomem *pbase;
	unsigned long usb_mac_base;
	unsigned long usb_phy11_base;

	INFO("[Flow][USB11]%s:%d,CONFIG_OF\n", __func__, __LINE__);
	node = of_find_compatible_node(NULL, NULL, "mediatek,mt8167-usb11");
	if (node == NULL)
		INFO("[Flow][USB11] get node failed\n");
	base = of_iomap(node, 0);
	usb1_irq_number = irq_of_parse_and_map(node, 0);
	pbase = of_iomap(node, 1);

	usb_mac_base = (unsigned long)base;
	usb_phy11_base = (unsigned long)pbase;
	irq = usb1_irq_number;

	INFO("[Flow][USB11]musb probe reg: 0x%lx ,usb_phy11_base == 0x%lx ,pbase == 0x%lx irq: %d\n",
	     usb_mac_base, usb_phy11_base, (unsigned long)pbase, usb1_irq_number);

#endif
	INFO("++\n");
	INFO("[Flow][USB11]%s: %d\n", __func__, __LINE__);

#ifndef CONFIG_MUSBFSH_PIO_ONLY	/*using DMA*/
	/* clobbered by use_dma=n */
	orig_dma_mask = dev->dma_mask;
#endif

#ifdef CONFIG_OF
	status = musbfsh_init_controller(dev, irq, base, (void __iomem *)pbase);
#else
	INFO("[Flow][USB11]%s:%d, base == %p\n", __func__, __LINE__, USB_BASE);
	base = (void *)USB_BASE;
	status = musbfsh_init_controller(dev, irq, base);
#endif


	if (status < 0)
		ERR("musbfsh_init_controller failed with status %d\n", status);
	INFO("--\n");
	#ifdef CONFIG_MUSBFSH_VBUS
	pdev_node = of_find_device_by_node(node);
	if (!pdev_node) {
		WARNING("ERROR: Cannot find usb pdev!\n");
		return PTR_ERR(node);
	}
	mt_pinctrl = devm_pinctrl_get(&pdev_node->dev);
	if (IS_ERR(mt_pinctrl))
		WARNING("ERROR: Cannot find usb pinctrl!\n");
	musbfsh_init_drvvbus();
	#endif
	#ifdef CONFIG_MUSBFSH_IDDIG
	pinctrl_select_state(mt_pinctrl, pinctrl_drvvbus1_low);
	usb11_iddig_pin = of_get_named_gpio(node, "iddig_gpio", 0);
	if (usb11_iddig_pin == 0)
		WARNING("iddig_gpio fail\n");
	WARNING("usb11 iddig_pin %x\n", usb11_iddig_pin);
	wake_lock_init(&usb11_lock, WAKE_LOCK_SUSPEND, "USB11 suspend lock");
	INIT_DELAYED_WORK(&musbfsh_Device->id_pin_work, musbfsh_id_pin_work);
	musbfsh_iddig_int();

	#endif
#ifdef IC_USB
	device_create_file(dev, &dev_attr_start);
	WARNING("IC-USB is enabled\n");
#endif
	INFO("[Flow][USB11]%s:%d end ere\n", __func__, __LINE__);

	return status;
}

static int musbfsh_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct musbfsh *musbfsh = dev_to_musbfsh(dev);
	void __iomem *ctrl_base = musbfsh->ctrl_base;

	INFO("++\n");
	/* this gets called on rmmod.
	 *  - Host mode: host may still be active
	 *  - Peripheral mode: peripheral is deactivated (or never-activated)
	 *  - OTG mode: both roles are deactivated (or never-activated)
	 */
	musbfsh_shutdown(pdev);
	if (musbfsh->board_mode == MUSBFSH_HOST)
		usb_remove_hcd(musbfsh_to_hcd(musbfsh));
	musbfsh_writeb(musbfsh->mregs, MUSBFSH_DEVCTL, 0);
	musbfsh_platform_exit(musbfsh);
	musbfsh_writeb(musbfsh->mregs, MUSBFSH_DEVCTL, 0);

	musbfsh_free(musbfsh);
	iounmap(ctrl_base);
	device_init_wakeup(&pdev->dev, 0);
#ifndef CONFIG_MUSBFSH_PIO_ONLY
	dma_set_mask(dev, *dev->parent->dma_mask);
#endif
	return 0;
}

#ifdef	CONFIG_PM

#ifdef MUSBFSH_ENABLE_BUS_SUSPEND
static struct musbfsh *g_musbfsh;
static void dump_op_state(const char *msg)
{
	int i;
	u8 reg;

	DBG(5, "%s:\n", msg);
	reg = musbfsh_readb(g_musbfsh->mregs, MUSBFSH_DEVCTL);
	DBG(5, "\tdevctl:0x%x\n", reg);

	DBG(5, "\t0x%x = 0x%x\n", 0x620, musbfsh_readl(g_musbfsh->mregs, 0x620));

	for (i = 0; i < 4; i++)
		DBG(5, "\t0x%x = 0x%x\n", 0x640 + i * 4, musbfsh_readl(g_musbfsh->mregs, 0x640 + i * 4));
}

static void musbfsh_force_utmi(struct musbfsh *musbfsh)
{
	void __iomem *musbfsh_base = musbfsh->mregs;
	u8 devctl;
	u32 utmi0, utmi1;
	u8 xcvr;
	u32 reg32;

	g_musbfsh = musbfsh;

	utmi0 = USB11PHY_READ32(0x68);
	DBG(5, "PHY 0x68-6B:0x%x\n", utmi0);
	utmi1 = USB11PHY_READ32(0x6C);
	DBG(5, "PHY 0x6C-6F:0x%x\n", utmi1);

	/* Force UTMI */
	devctl = musbfsh_readb(musbfsh->mregs, MUSBFSH_DEVCTL);
	DBG(5, "devctl : %d\n", devctl);
	if (devctl & MUSBFSH_DEVCTL_LSDEV) {
		/* LS */
		xcvr = 2;
	} else {		/* we think is: HS or FS */
		xcvr = 1;
	}
	DBG(5, "xcvr:%d\n", xcvr);

	utmi0 |= ((1 << RG_OPMODE) | (1 << RG_TERMSEL)
		  | (1 << RG_DPPULLDOWN) | (1 << RG_DMPULLDOWN)
		  | (xcvr << RG_XCVRSEL));
	utmi0 &= ~((1 << RG_TXVALID) | (1 << RG_TXVALID_H));
	USB11PHY_WRITE32(0x68, utmi0);

	utmi0 |= ((1 << FORCE_TERMSEL) | (1 << FORCE_TXVALID)
		  | (1 << FORCE_DPPULLDOWN) | (1 << FORCE_DMPULLDOWN)
		  | (1 << FORCE_XCVRSEL) | (1 << FORCE_OPMODE));
	USB11PHY_WRITE32(0x68, utmi0);

	utmi1 |= ((1 << RG_VBUSVALID) | (1 << FORCE_VBUSVALID));
	USB11PHY_WRITE32(0x6C, utmi1);

	utmi0 = USB11PHY_READ32(0x68);
	DBG(5, "PHY 0x68-6B:0x%x\n", utmi0);
	utmi1 = USB11PHY_READ32(0x6C);
	DBG(5, "PHY 0x6C-6F:0x%x\n", utmi1);

	reg32 = musbfsh_readl(musbfsh_base, MUSBFSH_HSDMA_INTR);
	DBG(5, "MUSBFSH_HSDMA_INTR:0x%x\n", reg32);
}

static void musbfsh_force_host_release_utmi(struct musbfsh *musbfsh)
{
	u8 devctl;
	u8 power, testmode;
	u32 reg32;
	u8 speed;

	g_musbfsh = musbfsh;

	/* Force Host & Release UTMI Flow */
	speed = musbfsh->context.speed;
	DBG(5, "hs_fs_ls:%d\n", speed);

	power = musbfsh_readb(musbfsh->mregs, MUSBFSH_POWER);
	DBG(5, "read power reg:0x%x\n", power);

	/* restore power */
	power = musbfsh->context.power;
	DBG(5, "restore power:0x%x\n", power);

	devctl = musbfsh_readb(musbfsh->mregs, MUSBFSH_DEVCTL);
	DBG(5, "devctl:0x%x\n", devctl);
	testmode = musbfsh_readb(musbfsh->mregs, MUSBFSH_TESTMODE);

	/* Reg:0x610 = 0x5 */
	musbfsh_writel(musbfsh->mregs, 0x610, 0x5);	/* add by chengchun */

	if (speed == 2) {	/* HS */
		musbfsh_writeb(musbfsh->mregs, MUSBFSH_TESTMODE, 0x90);
		musbfsh_writeb(musbfsh->mregs, MUSBFSH_DEVCTL,
			       musbfsh_readb(musbfsh->mregs, MUSBFSH_DEVCTL) | 0x01);

		DBG(5, "testmode:0x%x\n", testmode);
		dump_op_state("Before force SE0");

		/* Force SE0 */
		reg32 = USB11PHY_READ32(0x80);
		reg32 &= (0xFFFFFFFC);
		reg32 |= (1 << 2);	/* Bit2: maybe means FORCE_LINESTATE */
		USB11PHY_WRITE32(0x80, reg32);
		dump_op_state("after force SE0");

		/* suspend bus */
		power |= (MUSBFSH_POWER_SUSPENDM | MUSBFSH_POWER_ENSUSPEND);
		musbfsh_writeb(musbfsh->mregs, MUSBFSH_POWER, power);
		mdelay(15);

		dump_op_state("after suspend bus");

		reg32 = musbfsh_readl(musbfsh->mregs, 0x630);
		DBG(5, "0x630:0x%x\n", reg32);

		/* Clear Force SE0 */
		reg32 = USB11PHY_READ32(0x80);
		reg32 &= ~(1 << 2);	/* Bit2: maybe means FORCE_LINESTATE */
		USB11PHY_WRITE32(0x80, reg32);

		DBG(5, "Clear SE0 0x80:0x%x\n", reg32);
		dump_op_state("Clear force SE0");

		mdelay(4);
	} else {
		if (speed == 1)
			DBG(5, "speed:FS\n");
		else if (speed == 0)
			DBG(5, "speed:LS\n");

		if (speed == 1) {	/* FS */
			musbfsh_writeb(musbfsh->mregs, MUSBFSH_TESTMODE, 0xA0);
		} else if (speed == 0) {	/* LS */
			musbfsh_writeb(musbfsh->mregs, MUSBFSH_TESTMODE, 0x80);
		}

		reg32 = musbfsh_readb(musbfsh->mregs, MUSBFSH_TESTMODE);
		DBG(5, "testmode:%d\n", reg32);
		/* Session Bit: set 1 */
		musbfsh_writeb(musbfsh->mregs, MUSBFSH_DEVCTL, musbfsh_readb(musbfsh->mregs, MUSBFSH_DEVCTL) | 0x01);

		dump_op_state("before force SE0");
		/* Force SE0 */
		reg32 = USB11PHY_READ32(0x80);
		reg32 &= (0xFFFFFFFC);
		reg32 |= (1 << 2);	/* Bit2: maybe menas FORCE_LINESTATE */
		USB11PHY_WRITE32(0x80, reg32);
		dump_op_state("after force SE0");

		reg32 = USB11PHY_READ32(0x80);
		DBG(5, " FS/LS Force SE0 0x80:0x%x\n", reg32);

		reg32 = musbfsh_readl(musbfsh->mregs, 0x620);
		DBG(5, " 0x620:0x%x\n", reg32);

		/* Clear Force SE0 */
		reg32 = USB11PHY_READ32(0x80);
		reg32 &= ~(1 << 2);	/* Bit2: maybe means FORCE_LINESTATE */
		USB11PHY_WRITE32(0x80, reg32);
		dump_op_state("after clear force SE0");

		reg32 = USB11PHY_READ32(0x80);
		DBG(5, " Clear SE0 0x80:0x%x\n", reg32);

		/* suspend bus */
		power |= (MUSBFSH_POWER_SUSPENDM | MUSBFSH_POWER_ENSUSPEND);
		musbfsh_writeb(musbfsh->mregs, MUSBFSH_POWER, power);
		mdelay(15);
		dump_op_state("after suspend bus");

		power = musbfsh_readb(musbfsh->mregs, MUSBFSH_POWER);
		DBG(5, "power:0x%x\n", power);

		reg32 = musbfsh_readl(musbfsh->mregs, 0x620);
		DBG(5, " 0x620:0x%x\n", reg32);

		mdelay(3);
	}

	devctl = musbfsh_readb(musbfsh->mregs, MUSBFSH_DEVCTL);
	DBG(5, "devctl:0x%x\n", devctl);

	/* Release UTMI */
	reg32 = USB11PHY_READ32(0x68);
	reg32 &= ~((1 << FORCE_TERMSEL) | (1 << FORCE_TXVALID)
		   | (1 << FORCE_DPPULLDOWN) | (1 << FORCE_DMPULLDOWN)
		   | (1 << FORCE_XCVRSEL) | (1 << FORCE_OPMODE)
		   | (1 << FORCE_SUSPENDM));
	reg32 |= (1 << FORCE_SUSPENDM) | (1 << RG_SUSPENDM);
	USB11PHY_WRITE32(0x68, reg32);

	dump_op_state("After release UTMI");

	musbfsh_writeb(musbfsh->mregs, MUSBFSH_TESTMODE, 0x00);	/*Clear Test Mode */
	DBG(5, "0x630 = %x\n", musbfsh_readl(musbfsh->mregs, 0x630));
}
#endif

#ifdef MUSBFSH_ENABLE_BUS_SUSPEND

static void musbfsh_save_context(struct musbfsh *musbfsh)
{
	int i;
	void __iomem *musbfsh_base = musbfsh->mregs;
	void __iomem *epio;
	u8 devctl, power;
#ifdef MUSBFSH_ENABLE_BUS_SUSPEND
	int speed;
#endif

#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
	mtk11_dma_burst_setting = musbfsh_readl(musbfsh->mregs, 0x204);
	mtk11_qmu_ioc_setting = musbfsh_readl((musbfsh->mregs + MUSBFSH_QISAR), 0x30);
#endif

	DBG(0, "HDRC is %s\n", "Host");
	power = musbfsh_readb(musbfsh_base, MUSBFSH_POWER);
	devctl = musbfsh_readb(musbfsh_base, MUSBFSH_DEVCTL);
	DBG(5, "devctl : 0x%x\n", devctl);

	musbfsh->context.ep0_func_addr = musbfsh_readb(musbfsh_base, MUSBFSH_TXFUNCADDR);
	musbfsh->context.power = musbfsh_readb(musbfsh_base, MUSBFSH_POWER);
	musbfsh->context.intrtxe = musbfsh_readw(musbfsh_base, MUSBFSH_INTRTXE);
	musbfsh->context.intrrxe = musbfsh_readw(musbfsh_base, MUSBFSH_INTRRXE);
	musbfsh->context.intrusbe = musbfsh_readb(musbfsh_base, MUSBFSH_INTRUSBE);
	musbfsh->context.index = musbfsh_readb(musbfsh_base, MUSBFSH_INDEX);
	musbfsh->context.devctl = musbfsh_readb(musbfsh_base, MUSBFSH_DEVCTL);

	musbfsh->context.l1_int = musbfsh_readl(musbfsh_base, USB11_L1INTM);

#ifdef MUSBFSH_ENABLE_BUS_SUSPEND
	speed = (power & MUSBFSH_POWER_HSMODE) ? 2 : -1;
	if (speed == -1) {
		if (devctl & MUSBFSH_DEVCTL_FSDEV)
			speed = 1;
		else
			speed = 0;
	}

	musbfsh->context.speed = speed;
	DBG(5, "speed:%d, musbfsh speed	 mode:%s\n",
	    musbfsh->context.speed, speed == 2 ? ("HS") : (speed == 1 ? "FS" : "LS"));
#endif

	for (i = 0; i < MUSBFSH_C_NUM_EPS - 1; ++i) {
		struct musbfsh_hw_ep *hw_ep;

		hw_ep = &musbfsh->endpoints[i];
		if (!hw_ep)
			continue;

		epio = hw_ep->regs;
		if (!epio)
			continue;

		musbfsh_writeb(musbfsh_base, MUSBFSH_INDEX, i);
		musbfsh->context.index_regs[i].txmaxp = musbfsh_readw(epio, MUSBFSH_TXMAXP);
		musbfsh->context.index_regs[i].txcsr = musbfsh_readw(epio, MUSBFSH_TXCSR);
		musbfsh->context.index_regs[i].rxmaxp = musbfsh_readw(epio, MUSBFSH_RXMAXP);
		musbfsh->context.index_regs[i].rxcsr = musbfsh_readw(epio, MUSBFSH_RXCSR);
#ifdef MUSBFSH_ENABLE_BUS_SUSPEND
		musbfsh->context.index_regs[i].txfunaddr =
		    musbfsh_readb(epio, MUSBFSH_TXFUNCADDR + 8 * i);
		musbfsh->context.index_regs[i].txhubaddr =
		    musbfsh_readb(epio, MUSBFSH_TXHUBADDR + 8 * i);
		musbfsh->context.index_regs[i].rxfunaddr =
		    musbfsh_readb(epio, MUSBFSH_RXFUNCADDR + 8 * i);
		musbfsh->context.index_regs[i].rxhubaddr =
		    musbfsh_readb(epio, MUSBFSH_RXHUBADDR + 8 * i);
		DBG(5, "txfuncaddr:0x%x, txhubaddr:0x%x,rxfuncaddr:0x%x,rxhubaddr:0x%x\n",
		    musbfsh->context.index_regs[i].txfunaddr,
		    musbfsh->context.index_regs[i].txhubaddr,
		    musbfsh->context.index_regs[i].rxfunaddr,
		    musbfsh->context.index_regs[i].rxhubaddr);
#endif

		if (musbfsh->dyn_fifo) {
			musbfsh->context.index_regs[i].txfifoadd =
			    musbfsh_read_txfifoadd(musbfsh_base);
			musbfsh->context.index_regs[i].rxfifoadd =
			    musbfsh_read_rxfifoadd(musbfsh_base);
			musbfsh->context.index_regs[i].txfifosz =
			    musbfsh_read_txfifosz(musbfsh_base);
			musbfsh->context.index_regs[i].rxfifosz =
			    musbfsh_read_rxfifosz(musbfsh_base);
		}
	}
	DBG(5, "musbfsh->context.speed:%d\n", musbfsh->context.speed);
}

#else

static void musbfsh_save_context_default(struct musbfsh *musbfsh)
{
	int i;
	void __iomem *musbfsh_base = musbfsh->mregs;
	void __iomem *epio;
#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
	mtk11_dma_burst_setting = musbfsh_readl(musbfsh->mregs, 0x204);
	mtk11_qmu_ioc_setting = musbfsh_readl((musbfsh->mregs + MUSBFSH_QISAR), 0x30);
#endif
	musbfsh->context.power = musbfsh_readb(musbfsh_base, MUSBFSH_POWER);
	musbfsh->context.intrtxe = musbfsh_readw(musbfsh_base, MUSBFSH_INTRTXE);
	musbfsh->context.intrrxe = musbfsh_readw(musbfsh_base, MUSBFSH_INTRRXE);
	musbfsh->context.intrusbe = musbfsh_readb(musbfsh_base, MUSBFSH_INTRUSBE);
	musbfsh->context.index = musbfsh_readb(musbfsh_base, MUSBFSH_INDEX);
	musbfsh->context.devctl = musbfsh_readb(musbfsh_base, MUSBFSH_DEVCTL);
	musbfsh->context.l1_int = musbfsh_readl(musbfsh_base, USB11_L1INTM);
	for (i = 0; i < MUSBFSH_C_NUM_EPS - 1; ++i) {
		struct musbfsh_hw_ep *hw_ep;

		hw_ep = &musbfsh->endpoints[i];
		if (!hw_ep)
			continue;
		epio = hw_ep->regs;
		if (!epio)
			continue;
		musbfsh_writeb(musbfsh_base, MUSBFSH_INDEX, i);
		musbfsh->context.index_regs[i].txmaxp = musbfsh_readw(epio, MUSBFSH_TXMAXP);
		musbfsh->context.index_regs[i].txcsr = musbfsh_readw(epio, MUSBFSH_TXCSR);
		musbfsh->context.index_regs[i].rxmaxp = musbfsh_readw(epio, MUSBFSH_RXMAXP);
		musbfsh->context.index_regs[i].rxcsr = musbfsh_readw(epio, MUSBFSH_RXCSR);
		if (musbfsh->dyn_fifo) {
			musbfsh->context.index_regs[i].txfifoadd =
			    musbfsh_read_txfifoadd(musbfsh_base);
			musbfsh->context.index_regs[i].rxfifoadd =
			    musbfsh_read_rxfifoadd(musbfsh_base);
			musbfsh->context.index_regs[i].txfifosz =
			    musbfsh_read_txfifosz(musbfsh_base);
			musbfsh->context.index_regs[i].rxfifosz =
			    musbfsh_read_rxfifosz(musbfsh_base);
		}
	}
}

#endif

#ifdef MUSBFSH_ENABLE_BUS_SUSPEND
static void musbfsh_restore_context(struct musbfsh *musbfsh)
{
#ifdef MUSBFSH_ENABLE_BUS_SUSPEND
	int i;
	void __iomem *musbfsh_base = musbfsh->mregs;
	void __iomem *epio;

	u8 reg, devctl;
	u32 reg32;
#endif

	DBG(5, " HDRC is %s\n", "Host");

	/* Now, is host mode */
#ifdef MUSBFSH_ENABLE_BUS_SUSPEND
	/* bubble set */
	reg = musbfsh_readb(musbfsh->mregs, 0x74);
	/* DBG(5, "read 0x74 reg:0x%x\n", reg); */
	reg &= 0x3F;
	musbfsh_writeb(musbfsh->mregs, 0x74, reg);

	/* reg = musbfsh_readb(musbfsh->mregs, 0x74); */
	/* DBG(5, "after babble set, read 0x74 reg:0x%x\n", reg); */

	reg32 = musbfsh_readl(musbfsh->mregs, 0x620);
	DBG(5, "0x620:0x%x\n", reg32);

	/* Restore MAC Start */
	DBG(5,
	    "ep0_func_addr:0x%x, power:0x%x, intrusbe:0x%x, index:0x%x,	devctl:0x%x, l1_int:0x%x\n",
	    musbfsh->context.ep0_func_addr, musbfsh->context.power, musbfsh->context.intrusbe,
	    musbfsh->context.index, musbfsh->context.devctl, musbfsh->context.l1_int);

#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
	musbfsh_writel(musbfsh->mregs, 0x204, mtk11_dma_burst_setting);
	musbfsh_writel((musbfsh->mregs + MUSBFSH_QISAR), 0x30, mtk11_qmu_ioc_setting);
#endif

	musbfsh_writeb(musbfsh_base, MUSBFSH_TXFUNCADDR, musbfsh->context.ep0_func_addr);

	devctl = musbfsh_readb(musbfsh_base, MUSBFSH_DEVCTL);
	/* we don't restore DEVCTL:Bit0 Session */
	musbfsh_writeb(musbfsh_base, MUSBFSH_DEVCTL, ((musbfsh->context.devctl) & 0xFE) | (devctl & 0x01));

	for (i = 0; i < musbfsh->config->num_eps; ++i) {
		struct musbfsh_hw_ep *hw_ep;

		hw_ep = &musbfsh->endpoints[i];
		if (!hw_ep)
			continue;

		epio = hw_ep->regs;
		if (!epio)
			continue;
		DBG(5, "i:%d, epnum:%d\n", i, hw_ep->epnum);
		musbfsh_writeb(musbfsh_base, MUSBFSH_INDEX, i);
		musbfsh_writew(epio, MUSBFSH_TXMAXP, musbfsh->context.index_regs[i].txmaxp);
		musbfsh_writew(epio, MUSBFSH_TXCSR, musbfsh->context.index_regs[i].txcsr);
		musbfsh_writew(epio, MUSBFSH_RXMAXP, musbfsh->context.index_regs[i].rxmaxp);
		musbfsh_writew(epio, MUSBFSH_RXCSR, musbfsh->context.index_regs[i].rxcsr);

		musbfsh_writeb(epio, MUSBFSH_TXFUNCADDR + 8 * i,
			       musbfsh->context.index_regs[i].txfunaddr);
		musbfsh_writeb(epio, MUSBFSH_TXHUBADDR + 8 * i,
			       musbfsh->context.index_regs[i].txhubaddr);
		musbfsh_writeb(epio, MUSBFSH_RXFUNCADDR + 8 * i,
			       musbfsh->context.index_regs[i].rxfunaddr);
		musbfsh_writeb(epio, MUSBFSH_RXHUBADDR + 8 * i,
			       musbfsh->context.index_regs[i].rxhubaddr);
		DBG(5, "txfuncaddr:0x%x, txhubaddr:0x%x,rxfuncaddr:0x%x,rxhubaddr:0x%x\n",
		    musbfsh->context.index_regs[i].txfunaddr,
		    musbfsh->context.index_regs[i].txhubaddr,
		    musbfsh->context.index_regs[i].rxfunaddr,
		    musbfsh->context.index_regs[i].rxhubaddr);
		if (musbfsh->dyn_fifo) {
			musbfsh_write_txfifosz(musbfsh_base,
					       musbfsh->context.index_regs[i].txfifosz);
			musbfsh_write_rxfifosz(musbfsh_base,
					       musbfsh->context.index_regs[i].rxfifosz);
			musbfsh_write_txfifoadd(musbfsh_base,
						musbfsh->context.index_regs[i].txfifoadd);
			musbfsh_write_rxfifoadd(musbfsh_base,
						musbfsh->context.index_regs[i].rxfifoadd);
			DBG(5, "txfifoadd:0x%x, rxfifoadd:0x%x,txfifosz:0x%x,rxfifosz:0x%x\n",
			    musbfsh->context.index_regs[i].txfifoadd,
			    musbfsh->context.index_regs[i].rxfifoadd,
			    musbfsh->context.index_regs[i].txfifosz,
			    musbfsh->context.index_regs[i].rxfifosz);
		}
	}
	musbfsh_writeb(musbfsh_base, MUSBFSH_INDEX, musbfsh->context.index);
	DBG(5, "reg index:%d, store index:%d\n", reg, musbfsh->context.index);

	reg32 = musbfsh_readl(musbfsh_base, USB11_L1INTM);
#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
	musbfsh_writel(musbfsh->mregs, USB11_L1INTM, 0x002f);
#else
	musbfsh_writel(musbfsh->mregs, USB11_L1INTM, 0x000f);
#endif
	reg32 = musbfsh_readl(musbfsh_base, USB11_L1INTM);
	DBG(5, "USB_L1INTM:0x%x\n", reg32);

	/* Enable all interrupts at DMA */
	reg32 = musbfsh_readl(musbfsh_base, MUSBFSH_HSDMA_INTR);
	DBG(5, "before, MUSBFSH_HSDMA_INTR:0x%x\n", reg32);
	musbfsh_writel(musbfsh->mregs, MUSBFSH_HSDMA_INTR,
		       0xFF | (0xFF << DMA_INTR_UNMASK_SET_OFFSET));
	musbfsh_writew(musbfsh_base, MUSBFSH_INTRTXE, musbfsh->context.intrtxe);
	musbfsh_writew(musbfsh_base, MUSBFSH_INTRRXE, musbfsh->context.intrrxe);
	musbfsh_writeb(musbfsh_base, MUSBFSH_INTRUSBE,
		       MUSBFSH_INTR_CONNECT | MUSBFSH_INTR_BABBLE | MUSBFSH_INTR_SUSPEND |
		       MUSBFSH_INTR_SESSREQ | MUSBFSH_INTR_VBUSERROR | MUSBFSH_INTR_DISCONNECT |
		       MUSBFSH_INTR_RESUME);
	reg32 = musbfsh_readl(musbfsh_base, MUSBFSH_HSDMA_INTR);
	DBG(5, "after set, MUSBFSH_HSDMA_INTR:0x%x\n", reg32);
	reg = musbfsh_readb(musbfsh_base, MUSBFSH_INTRUSBE);
	DBG(5, "after set, MUSBFSH_INTRUSBE:0x%x\n", reg);
	/* Restore MAC End */
#endif
}

#else

static void musbfsh_restore_context_default(struct musbfsh *musbfsh)
{
	int i;
	void __iomem *musbfsh_base = musbfsh->mregs;
	void __iomem *epio;
#ifdef CONFIG_MTK_MUSBFSH_QMU_SUPPORT
	musbfsh_writel(musbfsh->mregs, 0x204, mtk11_dma_burst_setting);
	musbfsh_writel((musbfsh->mregs + MUSBFSH_QISAR), 0x30, mtk11_qmu_ioc_setting);
#endif
	musbfsh_writeb(musbfsh_base, MUSBFSH_POWER, musbfsh->context.power);
	musbfsh_writew(musbfsh_base, MUSBFSH_INTRTXE, musbfsh->context.intrtxe);
	musbfsh_writew(musbfsh_base, MUSBFSH_INTRRXE, musbfsh->context.intrrxe);
	musbfsh_writeb(musbfsh_base, MUSBFSH_INTRUSBE, musbfsh->context.intrusbe);
	musbfsh_writeb(musbfsh_base, MUSBFSH_DEVCTL, musbfsh->context.devctl);
	for (i = 0; i < MUSBFSH_C_NUM_EPS - 1; ++i) {
		struct musbfsh_hw_ep *hw_ep;

		hw_ep = &musbfsh->endpoints[i];
		if (!hw_ep)
			continue;
		epio = hw_ep->regs;
		if (!epio)
			continue;
		musbfsh_writeb(musbfsh_base, MUSBFSH_INDEX, i);
		musbfsh_writew(epio, MUSBFSH_TXMAXP, musbfsh->context.index_regs[i].txmaxp);
		musbfsh_writew(epio, MUSBFSH_TXCSR, musbfsh->context.index_regs[i].txcsr);
		musbfsh_writew(epio, MUSBFSH_RXMAXP, musbfsh->context.index_regs[i].rxmaxp);
		musbfsh_writew(epio, MUSBFSH_RXCSR, musbfsh->context.index_regs[i].rxcsr);
		if (musbfsh->dyn_fifo) {
			musbfsh_write_txfifosz(musbfsh_base,
					       musbfsh->context.index_regs[i].txfifosz);
			musbfsh_write_rxfifosz(musbfsh_base,
					       musbfsh->context.index_regs[i].rxfifosz);
			musbfsh_write_txfifoadd(musbfsh_base,
						musbfsh->context.index_regs[i].txfifoadd);
			musbfsh_write_rxfifoadd(musbfsh_base,
						musbfsh->context.index_regs[i].rxfifoadd);
		}
	}
	musbfsh_writeb(musbfsh_base, MUSBFSH_INDEX, musbfsh->context.index);
	mb();/* */
	/* Enable all interrupts at DMA
	 * Caution: The DMA Reg type is WRITE to SET or CLEAR
	 */
	musbfsh_writel(musbfsh->mregs, MUSBFSH_HSDMA_INTR,
		       0xFF | (0xFF << MUSBFSH_DMA_INTR_UNMASK_SET_OFFSET));
	musbfsh_writel(musbfsh_base, USB11_L1INTM, musbfsh->context.l1_int);
}

#endif

static bool is_usb11_clk_prepared;
/* static int usb11_clk_prepare_cnt; */
int mt_usb11_clock_prepare(void)
{
	int retval = 0;

	/* WARNING("prepared: %d\n", ++usb11_clk_prepare_cnt); */
	if (is_usb11_clk_prepared)
		return 0;


	INFO("mt_usb11_clock_prepare\n");

	retval = clk_prepare(usbpll_clk);
	if (retval)
		goto exit;
#if 0
	retval = clk_prepare(usb_clk);
	if (retval)
		goto exit;
#endif

	retval = clk_prepare(usbmcu_clk);
	if (retval)
		goto exit;
	retval = clk_prepare(icusb_clk);
	if (retval)
		goto exit;

	is_usb11_clk_prepared = true;
	return 0;
exit:
	WARNING("[USB11] clock prepare fail\n");
	return retval;
}

void mt_usb11_clock_unprepare(void)
{
	/* WARNING("prepared: %d\n", --usb11_clk_prepare_cnt); */
	if (!is_usb11_clk_prepared)
		return;

	INFO("mt_usb11_clock_unprepare\n");

	clk_unprepare(icusb_clk);
	clk_unprepare(usbmcu_clk);
	/*clk_unprepare(usb_clk);*/
	clk_unprepare(usbpll_clk);

	is_usb11_clk_prepared = 0;
}

static int musbfsh_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	unsigned long flags;
	struct musbfsh *musbfsh = dev_to_musbfsh(&pdev->dev);

	WARNING("+\n");
#if (defined(CONFIG_MUSBFSH_VBUS) && !defined(CONFIG_MUSBFSH_IDDIG))
	mtk11_suspend = true;
#elif (defined(CONFIG_MUSBFSH_VBUS) && defined(CONFIG_MUSBFSH_IDDIG))
#ifndef MUSBFSH_ENABLE_BUS_SUSPEND
	musbfsh_power = true;
	mt_usb11_clock_prepare();
	mt65xx_usb11_clock_enable(true);
#endif
#elif (!defined(CONFIG_MUSBFSH_VBUS) && !defined(CONFIG_MUSBFSH_IDDIG))
#ifndef MUSBFSH_ENABLE_BUS_SUSPEND
	musbfsh_power = true;
	mt_usb11_clock_prepare();
	mt65xx_usb11_clock_enable(true);
#endif

#endif
	spin_lock_irqsave(&musbfsh->lock, flags);

#ifdef MUSBFSH_ENABLE_BUS_SUSPEND
	musbfsh_save_context(musbfsh);
#else
	musbfsh_save_context_default(musbfsh);
#endif

#ifdef MUSBFSH_ENABLE_BUS_SUSPEND
	musbfsh_force_utmi(musbfsh);
#endif

#ifdef MUSBFSH_ENABLE_BUS_SUSPEND
	/* disable interrupts */
	musbfsh_generic_disable(musbfsh);
#endif

#if (defined(CONFIG_MUSBFSH_VBUS) && !defined(CONFIG_MUSBFSH_IDDIG))
	pinctrl_select_state(mt_pinctrl, pinctrl_drvvbus1_low);
	mdelay(1000);
	WARNING("+++\n");
	musbfsh_platform_set_power(musbfsh, 0);
#elif (defined(CONFIG_MUSBFSH_VBUS) && defined(CONFIG_MUSBFSH_IDDIG))
	mt65xx_usb11_phy_savecurrent();
#elif (!defined(CONFIG_MUSBFSH_VBUS) && !defined(CONFIG_MUSBFSH_IDDIG))
	mt65xx_usb11_phy_savecurrent();
#else
	musbfsh_platform_set_power(musbfsh, 0);
#endif

	spin_unlock_irqrestore(&musbfsh->lock, flags);

#if (defined(CONFIG_MUSBFSH_VBUS) && !defined(CONFIG_MUSBFSH_IDDIG))
	if (!mtk11_with_device) {
		mt_usb11_clock_unprepare();
		WARNING("unprepare++\n");
	}
#elif (defined(CONFIG_MUSBFSH_VBUS) && defined(CONFIG_MUSBFSH_IDDIG))
	mt65xx_usb11_clock_enable(false);
	musbfsh_power = false;
	mt_usb11_clock_unprepare();
#elif (!defined(CONFIG_MUSBFSH_VBUS) && !defined(CONFIG_MUSBFSH_IDDIG))
	mt65xx_usb11_clock_enable(false);
	musbfsh_power = false;
	mt_usb11_clock_unprepare();

#endif

	WARNING("-\n");
	return 0;
}

static int musbfsh_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	unsigned long flags;
	struct musbfsh *musbfsh = dev_to_musbfsh(&pdev->dev);
#if (defined(CONFIG_MUSBFSH_VBUS) && !defined(CONFIG_MUSBFSH_IDDIG))
	int retval = 0;

	WARNING("+\n");
	pinctrl_select_state(mt_pinctrl, pinctrl_drvvbus1_high);
	mtk11_suspend = false;
	mtk11_with_device = false;
	retval = mt_usb11_clock_prepare();
	if (retval) {
		WARNING("!!musbfsh clock prepre fail,need to check!!\n");
		return retval;
	}
#elif (defined(CONFIG_MUSBFSH_VBUS) && defined(CONFIG_MUSBFSH_IDDIG))
	musbfsh_power = true;
	mt_usb11_clock_prepare();
	mt65xx_usb11_clock_enable(true);
#elif (!defined(CONFIG_MUSBFSH_VBUS) && !defined(CONFIG_MUSBFSH_IDDIG))
	musbfsh_power = true;
	mt_usb11_clock_prepare();
	mt65xx_usb11_clock_enable(true);
#endif

	spin_lock_irqsave(&musbfsh->lock, flags);

#if defined(CONFIG_MUSBFSH_IDDIG)
	mt65xx_usb11_phy_recover();
#elif (!defined(CONFIG_MUSBFSH_VBUS) && !defined(CONFIG_MUSBFSH_IDDIG))
	mt65xx_usb11_phy_recover();
#else
	musbfsh_platform_set_power(musbfsh, 1);
#endif

#ifdef MUSBFSH_ENABLE_BUS_SUSPEND
	musbfsh_restore_context(musbfsh);
#else
	musbfsh_restore_context_default(musbfsh);
#endif

#ifdef MUSBFSH_ENABLE_BUS_SUSPEND
	musbfsh_force_host_release_utmi(musbfsh);
#endif

	spin_unlock_irqrestore(&musbfsh->lock, flags);

#if (defined(CONFIG_MUSBFSH_VBUS) && defined(CONFIG_MUSBFSH_IDDIG))
#ifndef MUSBFSH_ENABLE_BUS_SUSPEND
	mt65xx_usb11_clock_enable(false);
	musbfsh_power = false;
	mt_usb11_clock_unprepare();
#endif
#elif (!defined(CONFIG_MUSBFSH_VBUS) && !defined(CONFIG_MUSBFSH_IDDIG))
#ifndef MUSBFSH_ENABLE_BUS_SUSPEND
	mt65xx_usb11_clock_enable(false);
	musbfsh_power = false;
	mt_usb11_clock_unprepare();
#endif

#endif

	WARNING("-\n");
	return 0;
}

static const struct dev_pm_ops musbfsh_dev_pm_ops = {
	.suspend = musbfsh_suspend,
	.resume = musbfsh_resume,
};

#define MUSBFSH_DEV_PM_OPS (&musbfsh_dev_pm_ops)
#else
#define	MUSBFSH_DEV_PM_OPS	NULL
#endif

static struct platform_driver musbfsh_driver = {
	.driver = {
		   .name = (char *)musbfsh_driver_name,
		   .bus = &platform_bus_type,
		   .of_match_table = apusb_of_ids,
		   .owner = THIS_MODULE,
		   .pm = MUSBFSH_DEV_PM_OPS,
		   },
	.probe = musbfsh_probe,
	.remove = musbfsh_remove,
	.shutdown = musbfsh_shutdown,
};


/*-------------------------------------------------------------------------*/
static int __init musbfsh_init(void)
{
	if (usb_disabled())	/*based on the config variable.*/
		return 0;

	WARNING("MUSBFSH is enabled\n");
	INFO("[Flow][USB11]%s:%d\n", __func__, __LINE__);

	usb11_init();

	return platform_driver_register(&musbfsh_driver);
}

/* make us init after usbcore and i2c (transceivers, regulators, etc)
 * and before usb gadget and host-side drivers start to register
*/
late_initcall_sync(musbfsh_init);

static void __exit musbfsh_cleanup(void)
{
	/*wake_lock_destroy(&musbfsh_suspend_lock);*/
	platform_driver_unregister(&musbfsh_driver);
	usb11_exit();
}

module_exit(musbfsh_cleanup);

int musbfsh_debug;

module_param(musbfsh_debug, int, 0644);
