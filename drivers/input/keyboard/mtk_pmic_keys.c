/*
 * Copyright (C) 2016 MediaTek, Inc.
 *
 * Author: Chen Zhong <chen.zhong@mediatek.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/wakelock.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/mfd/mt6323/registers.h>
#include <linux/mfd/mt6397/registers.h>
#include <linux/mfd/mt6392/registers.h>
#include <linux/mfd/mt6397/core.h>
#include <linux/slab.h>
#include <linux/irqdomain.h>

#ifdef CONFIG_KEYBOARD_MTK_PMIC_GPIO
#include <linux/gpio.h>
#include <linux/irq_sim.h>
#endif // CONFIG_KEYBOARD_MTK_PMIC_GPIO

#define PMIC_RG_PWRKEY_RST_EN_MASK  0x1
#define PMIC_RG_PWRKEY_RST_EN_SHIFT  6
#define PMIC_RG_HOMEKEY_RST_EN_MASK  0x1
#define PMIC_RG_HOMEKEY_RST_EN_SHIFT  5
#define PMIC_RG_RST_DU_MASK  0x3
#define PMIC_RG_RST_DU_SHIFT  8

static struct wake_lock pwrkey_lock;
#define PWRKEY_INITIAL_STATE (0)

struct pmic_keys_regs {
	u32 deb_reg;
	u32 deb_mask;
	u32 intsel_reg;
	u32 intsel_mask;
};

#define PMIC_KEYS_REGS(_deb_reg, _deb_mask, _intsel_reg, _intsel_mask)	\
{									\
	.deb_reg		= _deb_reg,				\
	.deb_mask		= _deb_mask,				\
	.intsel_reg		= _intsel_reg,				\
	.intsel_mask		= _intsel_mask,				\
}

#ifdef CONFIG_KEYBOARD_MTK_PMIC_GPIO
struct pmic_gpiochip {
	struct gpio_chip gc;
	struct irq_sim irqsim;
	struct pmic_keys_info *info;
	unsigned int irq_offset;
};

static DEFINE_SPINLOCK(pmic_gpiochip_slock);
static struct pmic_gpiochip *pmic_gpiochip_dev = NULL;
static unsigned int pmic_gpiochip_base = 0;
static unsigned int pmic_homekey_code = 0;
#endif

struct pmic_regs {
	const struct pmic_keys_regs pwrkey_regs;
	const struct pmic_keys_regs homekey_regs;
	u32 pmic_rst_reg;
	u8 indie_release_irq_mask;
};

static const struct pmic_regs mt6397_regs = {
	.pwrkey_regs = PMIC_KEYS_REGS(MT6397_CHRSTATUS, 0x8, MT6397_INT_RSV, 0x10),
	.homekey_regs = PMIC_KEYS_REGS(MT6397_OCSTATUS2, 0x10, MT6397_INT_RSV, 0x8),
	.pmic_rst_reg = MT6397_TOP_RST_MISC,
	.indie_release_irq_mask = 0,
};

static const struct pmic_regs mt6323_regs = {
	.pwrkey_regs = PMIC_KEYS_REGS(MT6323_CHRSTATUS, 0x2, MT6323_INT_MISC_CON, 0x10),
	.homekey_regs = PMIC_KEYS_REGS(MT6323_CHRSTATUS, 0x4, MT6323_INT_MISC_CON, 0x8),
	.pmic_rst_reg = MT6323_TOP_RST_MISC,
	.indie_release_irq_mask = 0,
};

static const struct pmic_regs mt6392_regs = {
	.pwrkey_regs = PMIC_KEYS_REGS(MT6392_CHRSTATUS, 0x2, MT6392_INT_MISC_CON, 0x10),
	.homekey_regs = PMIC_KEYS_REGS(MT6392_CHRSTATUS, 0x4, MT6392_INT_MISC_CON, 0x8),
	.pmic_rst_reg = MT6392_TOP_RST_MISC,
	.indie_release_irq_mask = 1,
};


struct pmic_keys_info {
	struct mtk_pmic_keys *keys;
	const struct pmic_keys_regs *regs;
	int keycode;
	int irq;
	u32 hw_irq;
	bool is_gpio;
};

struct mtk_pmic_keys {
	struct input_dev *input_dev;
	struct device *dev;
	struct regmap *regmap;
	struct irq_domain *irq_domain;
	struct pmic_keys_info pwrkey, homekey, pwr_release, home_release;
	u8 indie_release_irq_mask;
};

enum long_press_mode {
	LP_DISABLE,
	LP_ONEKEY,
	LP_TWOKEY,
};

static void mtk_pmic_long_press_reset_setup(struct mtk_pmic_keys *keys, u32 pmic_rst_reg)
{
	int ret;
	u32 long_press_mode, long_press_duration;

	ret = of_property_read_u32(keys->dev->of_node,
		"mediatek,long-press-duration", &long_press_duration);
	if (ret)
		long_press_duration = 0;

	regmap_update_bits(keys->regmap, pmic_rst_reg,
		PMIC_RG_RST_DU_MASK << PMIC_RG_RST_DU_SHIFT,
		long_press_duration << PMIC_RG_RST_DU_SHIFT);
	regmap_update_bits(keys->regmap, pmic_rst_reg,
		PMIC_RG_PWRKEY_RST_EN_MASK << PMIC_RG_PWRKEY_RST_EN_SHIFT,
		PMIC_RG_PWRKEY_RST_EN_MASK << PMIC_RG_PWRKEY_RST_EN_SHIFT);

	ret = of_property_read_u32(keys->dev->of_node,
		"mediatek,long-press-mode", &long_press_mode);

	if (!ret && long_press_mode == LP_ONEKEY) {
		regmap_update_bits(keys->regmap, pmic_rst_reg,
			PMIC_RG_HOMEKEY_RST_EN_MASK << PMIC_RG_HOMEKEY_RST_EN_SHIFT, 0);
	} else if (!ret && long_press_mode == LP_TWOKEY) {
		regmap_update_bits(keys->regmap, pmic_rst_reg,
			PMIC_RG_HOMEKEY_RST_EN_MASK << PMIC_RG_HOMEKEY_RST_EN_SHIFT,
			PMIC_RG_HOMEKEY_RST_EN_MASK << PMIC_RG_HOMEKEY_RST_EN_SHIFT);
	} else {
		regmap_update_bits(keys->regmap, pmic_rst_reg,
			PMIC_RG_PWRKEY_RST_EN_MASK << PMIC_RG_PWRKEY_RST_EN_SHIFT, 0);
		regmap_update_bits(keys->regmap, pmic_rst_reg,
			PMIC_RG_HOMEKEY_RST_EN_MASK << PMIC_RG_HOMEKEY_RST_EN_SHIFT, 0);
	}
}

static irqreturn_t mtk_pmic_keys_irq_handler_thread(int irq, void *data)
{
	struct pmic_keys_info *info = data;
	u32 key_deb, pressed;

#ifdef CONFIG_KEYBOARD_MTK_PMIC_GPIO
	if (info->is_gpio && pmic_gpiochip_dev) {
		irq_sim_fire(&pmic_gpiochip_dev->irqsim, pmic_gpiochip_dev->irq_offset);
		dev_info(info->keys->dev, "[PMICKEYS] Firing IRQ handler for GPIO.\n");
	} else {
#endif // CONFIG_KEYBOARD_MTK_PMIC_GPIO
		regmap_read(info->keys->regmap, info->regs->deb_reg, &key_deb);

		key_deb &= info->regs->deb_mask;

		pressed = !key_deb;

		input_report_key(info->keys->input_dev, info->keycode, pressed);
		input_sync(info->keys->input_dev);
		if (pressed)
			wake_lock(&pwrkey_lock);
		else
			wake_lock_timeout(&pwrkey_lock, HZ/2);

		dev_info(info->keys->dev, "[PMICKEYS] (%s) key =%d using PMIC\n",
			 pressed ? "pressed" : "released", info->keycode);
#ifdef CONFIG_KEYBOARD_MTK_PMIC_GPIO
	}
#endif // CONFIG_KEYBOARD_MTK_PMIC_GPIO

	return IRQ_HANDLED;
}

static int mtk_pmic_key_setup(struct mtk_pmic_keys *keys,
		const char *propname, struct pmic_keys_info *info, bool wakeup)
{
	int ret;

	ret = of_property_read_u32(keys->dev->of_node, propname, &info->keycode);
	if (ret)
		return 0;

	if (!info->keycode)
		return 0;

	info->keys = keys;
	info->is_gpio = false;

	if (keys->indie_release_irq_mask == 1) {
		ret = regmap_update_bits(keys->regmap, info->regs->intsel_reg,
			info->regs->intsel_mask, 0);
		if (ret < 0)
			return ret;
	} else {
		ret = regmap_update_bits(keys->regmap, info->regs->intsel_reg,
			info->regs->intsel_mask, info->regs->intsel_mask);
		if (ret < 0)
			return ret;
	}

	info->irq = irq_create_mapping(keys->irq_domain, info->hw_irq);
	if (info->irq <= 0)
		return -EINVAL;

	ret = devm_request_threaded_irq(keys->dev, info->irq, NULL,
				   mtk_pmic_keys_irq_handler_thread,
				   IRQF_ONESHOT | IRQF_TRIGGER_HIGH,
				   "mtk-pmic-keys", info);
	if (ret) {
		dev_err(keys->dev, "Failed to request IRQ: %d: %d\n",
			info->irq, ret);
		return ret;
	}

	if (wakeup)
		irq_set_irq_wake(info->irq, 1);

	__set_bit(info->keycode, keys->input_dev->keybit);

	return 0;
}

static void mtk_pmic_keys_dispose_irq(struct mtk_pmic_keys *keys)
{
	if (keys->pwrkey.irq)
		irq_dispose_mapping(keys->pwrkey.irq);

	if (keys->homekey.irq)
		irq_dispose_mapping(keys->homekey.irq);
}

static const struct of_device_id of_pmic_keys_match_tbl[] = {
	{
		.compatible = "mediatek,mt6397-keys",
		.data = &mt6397_regs,
	}, {
		.compatible = "mediatek,mt6323-keys",
		.data = &mt6323_regs,
	}, {
		.compatible = "mediatek,mt6392-keys",
		.data = &mt6392_regs,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_pmic_keys_match_tbl);

#ifdef CONFIG_KEYBOARD_MTK_PMIC_GPIO
static int pmic_gpiochip_direction_input(struct gpio_chip *gc, unsigned offset)
{
	return 0;
}

static int pmic_gpiochip_direction_output(struct gpio_chip *gc, unsigned offset, int value)
{
	return -EINVAL;
}

static void pmic_gpiochip_get_int_status(struct pmic_keys_info *info, bool *status)
{
	int ret;
	u32 key_deb;
	unsigned long flags;

	spin_lock_irqsave(&pmic_gpiochip_slock, flags);
	ret = regmap_read(info->keys->regmap, info->regs->deb_reg, &key_deb);
	key_deb &= info->regs->deb_mask;
	spin_unlock_irqrestore(&pmic_gpiochip_slock, flags);

	*status = !key_deb;
}

static int pmic_gpiochip_get_value(struct gpio_chip *gc, unsigned offset)
{
	bool gpio_value = false;
	/* report the state of the interrupt register */
	if (pmic_gpiochip_dev && pmic_gpiochip_dev->info) {
		pmic_gpiochip_get_int_status(pmic_gpiochip_dev->info, &gpio_value);
	}
	return gpio_value;
}

static void pmic_gpiochip_set_value(struct gpio_chip *gc, unsigned offset, int value)
{
	return;
}

static int pmic_gpiochip_to_irq(struct gpio_chip *gc, unsigned offset)
{
	unsigned long flags;
	if (!pmic_gpiochip_dev) {
		return -EINVAL;
	}
	/* save the offset */
	spin_lock_irqsave(&pmic_gpiochip_slock, flags);
	pmic_gpiochip_dev->irq_offset = offset;
	spin_unlock_irqrestore(&pmic_gpiochip_slock, flags);
	return irq_sim_irqnum(&pmic_gpiochip_dev->irqsim, offset);
}

#endif // CONFIG_KEYBOARD_MTK_PMIC_GPIO

static int mtk_pmic_keys_probe(struct platform_device *pdev)
{
	int ret;
	struct resource *res;
	struct mt6397_chip *pmic_chip = dev_get_drvdata(pdev->dev.parent);
	struct mtk_pmic_keys *keys;
	const struct pmic_regs *pmic_regs;
	struct input_dev *input_dev;
	const struct of_device_id *of_id =
		of_match_device(of_pmic_keys_match_tbl, &pdev->dev);

	keys = devm_kzalloc(&pdev->dev, sizeof(struct mtk_pmic_keys), GFP_KERNEL);
	if (!keys)
		return -ENOMEM;

	keys->dev = &pdev->dev;
	keys->regmap = pmic_chip->regmap;
	keys->irq_domain = pmic_chip->irq_domain;

	pmic_regs = of_id->data;
	keys->pwrkey.regs = &pmic_regs->pwrkey_regs;
	keys->homekey.regs = &pmic_regs->homekey_regs;
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "no IRQ resource\n");
		return -ENODEV;
	}

	keys->indie_release_irq_mask = pmic_regs->indie_release_irq_mask;
	keys->pwrkey.hw_irq = res->start;
	if (keys->indie_release_irq_mask == 1) {
		keys->pwr_release.regs = &pmic_regs->pwrkey_regs;
		keys->home_release.regs = &pmic_regs->homekey_regs;
		keys->homekey.hw_irq = res->end - 2;
		keys->pwr_release.hw_irq = res->end - 1;
		keys->home_release.hw_irq = res->end;
	} else {
		keys->homekey.hw_irq = res->end;
	}

	wake_lock_init(&pwrkey_lock, WAKE_LOCK_SUSPEND, "pwrkey wakelock");

	keys->input_dev = input_dev = devm_input_allocate_device(keys->dev);
	if (!input_dev) {
		dev_err(&pdev->dev, "[PMICKEYS] input allocate device fail.\n");
		return -ENOMEM;
	}

	input_dev->name = "mtk-pmic-keys";
	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0001;
	input_dev->dev.parent = &pdev->dev;

	__set_bit(EV_KEY, input_dev->evbit);

	ret = mtk_pmic_key_setup(keys, "mediatek,pwrkey-code", &keys->pwrkey, true);
	if (ret)
		goto out_dispose_irq;

	ret = mtk_pmic_key_setup(keys, "mediatek,homekey-code", &keys->homekey, false);
	if (ret)
		goto out_dispose_irq;

	if (keys->indie_release_irq_mask == 1) {
		ret = mtk_pmic_key_setup(keys, "mediatek,pwrkey-code", &keys->pwr_release, true);
		if (ret)
			goto out_dispose_irq;

		ret = mtk_pmic_key_setup(keys, "mediatek,homekey-code", &keys->home_release, false);
		if (ret)
			goto out_dispose_irq;
	}

#ifdef CONFIG_KEYBOARD_MTK_PMIC_GPIO
	ret = of_property_read_u32(pdev->dev.of_node, "mediatek,homekey-gpio", &pmic_gpiochip_base);
	if (!ret && !pmic_gpiochip_dev) {
		/* homekey-code must be provided to setup irqs above */
		ret = of_property_read_u32(pdev->dev.of_node, "mediatek,homekey-code", &pmic_homekey_code);
		if (ret) {
			dev_err(&pdev->dev, "%s: cannot setup gpio without homekey-code specified", __func__);
			ret = -EINVAL;
			goto out_dispose_irq;
		}
		pmic_gpiochip_dev = devm_kzalloc(&pdev->dev, sizeof(struct pmic_gpiochip), GFP_KERNEL);
		if (!pmic_gpiochip_dev) {
			dev_err(&pdev->dev, "%s: failed to allocate pmic_gpiochip_dev", __func__);
			ret = -ENOMEM;
			goto out_dispose_irq;
		}

		/* set reasonable defaults for gpio state and IRQ offset */
		keys->homekey.is_gpio = true;
		keys->home_release.is_gpio = true;
		pmic_gpiochip_dev->irq_offset = 0;
		pmic_gpiochip_dev->info = &keys->homekey;

		/* populate gpiochip parameters */
		pmic_gpiochip_dev->gc.base = pmic_gpiochip_base;
		pmic_gpiochip_dev->gc.ngpio = 1;
		pmic_gpiochip_dev->gc.label = "mtk-pmic-gpiobase";
		pmic_gpiochip_dev->gc.direction_input = pmic_gpiochip_direction_input;
		pmic_gpiochip_dev->gc.direction_output = pmic_gpiochip_direction_output;
		pmic_gpiochip_dev->gc.get = pmic_gpiochip_get_value;
		pmic_gpiochip_dev->gc.set = pmic_gpiochip_set_value;
		pmic_gpiochip_dev->gc.to_irq = pmic_gpiochip_to_irq;
		pmic_gpiochip_dev->gc.can_sleep = 0;
		pmic_gpiochip_dev->gc.dev = &pdev->dev;

		/* register gpiochip and simulated interrupt handler */
		ret = gpiochip_add(&pmic_gpiochip_dev->gc);
		if (ret) {
			dev_err(&pdev->dev, "%s: failed to add mtk-pmic-gpiochip, ret=%d\n", __func__, ret);
			goto out_dispose_irq;
		}
		ret = devm_irq_sim_init(&pdev->dev, &pmic_gpiochip_dev->irqsim, pmic_gpiochip_dev->gc.ngpio);
		if (ret < 0) {
			dev_err(&pdev->dev, "%s: failed to int mtk-pmic-gpiochip interrupt, ret=%d\n", __func__, ret);
			gpiochip_remove(&pmic_gpiochip_dev->gc);
			goto out_dispose_irq;
		}
	}
#endif // CONFIG_KEYBOARD_MTK_PMIC_GPIO

	ret = input_register_device(input_dev);
	if (ret) {
		dev_err(&pdev->dev, "[PMICKEYS] register input device failed (%d)\n", ret);
		input_free_device(input_dev);
		return ret;
	}

	input_set_drvdata(input_dev, keys);

	mtk_pmic_long_press_reset_setup(keys, pmic_regs->pmic_rst_reg);

	return 0;

out_dispose_irq:
	mtk_pmic_keys_dispose_irq(keys);
	return ret;
}

static int mtk_pmic_keys_remove(struct platform_device *pdev)
{
	struct mtk_pmic_keys *keys = platform_get_drvdata(pdev);

	mtk_pmic_keys_dispose_irq(keys);

#ifdef CONFIG_KEYBOARD_MTK_PMIC_GPIO
	if (pmic_gpiochip_dev) {
		irq_sim_fini(&pmic_gpiochip_dev->irqsim);
		gpiochip_remove(&pmic_gpiochip_dev->gc);
		kfree(pmic_gpiochip_dev);
		pmic_gpiochip_dev = NULL;
	}
#endif // CONFIG_KEYBOARD_MTK_PMIC_GPIO

	input_unregister_device(keys->input_dev);

	return 0;
}

static struct platform_driver pmic_keys_pdrv = {
	.probe = mtk_pmic_keys_probe,
	.remove = mtk_pmic_keys_remove,
	.driver = {
		   .name = "mtk-pmic-keys",
		   .owner = THIS_MODULE,
		   .of_match_table = of_pmic_keys_match_tbl,
	},
};

module_platform_driver(pmic_keys_pdrv);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Chen Zhong <chen.zhong@mediatek.com>");
MODULE_DESCRIPTION("MTK pmic-keys driver v0.1");
MODULE_ALIAS("keypad:pmic");
