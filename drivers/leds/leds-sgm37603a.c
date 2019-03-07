/*
 * Author: Xi.Chen (xixi.chen@mediatek.com)
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

/* #define DEBUG */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/leds-sgm37603a.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#define SGM37603A_BRIGHTNESS_CTRL1	0x1A
#define SGM37603A_BRIGHTNESS_CTRL2	0x19

#define SGM37603A_DEVICE_CTRL		0x11

/* SGM37603A Registers */
#define SGM37603A_BL_CMD			0x10
#define SGM37603A_BL_MASK			0x01
#define SGM37603A_BL_ON				0x01
#define SGM37603A_BL_OFF			0x00

#define BUF_SIZE		20
#define DEFAULT_BL_NAME		"lcd-backlight"
#define MAX_BRIGHTNESS		255

struct sgm37603a;

struct sgm37603a_device_config {
	u8 reg_brightness1;
	u8 reg_brightness2;
	u8 reg_devicectrl;
};

struct  sgm37603a_led_data {
	struct led_classdev bl;
	struct sgm37603a *lp;
};
struct sgm37603a {
	const char *chipname;
	enum sgm37603a_chip_id chip_id;
	struct sgm37603a_device_config *cfg;
	struct i2c_client *client;
	struct sgm37603a_led_data *led_data;
	struct device *dev;
	u8 last_brightness;
	struct mutex bl_update_lock;
	/* platform device data */
	enum sgm37603a_brightness_ctrl_mode mode;
	u8 device_control;
	int initial_brightness;
	int max_brightness;
	int brightness_limit;
	struct sgm37603a_pwm_data pwm_data;
	u8 load_new_rom_data;
	int size_program;
	struct sgm37603a_rom_data *rom_data;
	int gpio_en;
};

static unsigned char led_vmax;

static int __init setup_led_vmax(char *str)
{
	int vmax;

	if (get_option(&str, &vmax))
		led_vmax = (unsigned char)vmax;
	return 0;
}
__setup("sgm37603a_vmax=", setup_led_vmax);

static int sgm37603a_write_byte(struct sgm37603a *lp, u8 reg, u8 data)
{
	dev_info(lp->dev, "sgm37603a_write_byte: reg:0x%x value:0x%x\n", reg, data);
	return i2c_smbus_write_byte_data(lp->client, reg, data);
}

#if 0
static int sgm37603a_read_byte(struct sgm37603a *lp, u8 reg, u8 *data)
{
	int ret;

	ret = i2c_smbus_read_byte_data(lp->client, reg);
	if (ret < 0) {
		dev_err(lp->dev, "failed to read 0x%.2x\n", reg);
		return ret;
	}

	*data = (u8)ret;

	dev_info(lp->dev, "sgm37603a_read_byte: reg:0x%x value:0x%x\n", reg, *data);
	return 0;
}
#endif

static int sgm37603a_update_bit(struct sgm37603a *lp, u8 reg, u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	ret = i2c_smbus_read_byte_data(lp->client, reg);
	if (ret < 0) {
		dev_err(lp->dev, "failed to read 0x%.2x\n", reg);
		return ret;
	}

	tmp = (u8)ret;
	tmp &= ~mask;
	tmp |= data & mask;

	return sgm37603a_write_byte(lp, reg, tmp);
}

static int sgm37603a_init_device(struct sgm37603a *lp)
{
	int ret = 0;

	dev_dbg(lp->dev, "Power on SGM37603A\n");

	if (lp->gpio_en  >= 0) {
		gpio_direction_output(lp->gpio_en, 1);
		msleep(20);
	}

	return ret;
}

static int sgm37603a_bl_off(struct sgm37603a *lp)
{
	dev_dbg(lp->dev, "BL_ON = 0 before updating EPROM settings\n");
	/* BL_ON = 0 before updating EPROM settings */
	return sgm37603a_update_bit(lp, SGM37603A_BL_CMD, SGM37603A_BL_MASK,
				SGM37603A_BL_OFF);
}

static int sgm37603a_bl_on(struct sgm37603a *lp)
{
	dev_info(lp->dev, "BL_ON = 1 after updating EPROM settings\n");
	/* BL_ON = 1 after updating EPROM settings */
	return sgm37603a_update_bit(lp, SGM37603A_BL_CMD, SGM37603A_BL_MASK,
				SGM37603A_BL_ON);
}

static struct sgm37603a_device_config sgm37603a_cfg = {
	.reg_brightness1 = SGM37603A_BRIGHTNESS_CTRL1,
	.reg_brightness2 = SGM37603A_BRIGHTNESS_CTRL2,
	.reg_devicectrl = SGM37603A_DEVICE_CTRL,
};

static int sgm37603a_configure(struct sgm37603a *lp)
{
	switch (lp->chip_id) {
	case SGM37603A:
	   dev_dbg(lp->dev, "Load sgm37603a configuration\n");
		lp->cfg = &sgm37603a_cfg;
		break;
	default:
		return -EINVAL;
	}

	return sgm37603a_init_device(lp);
}

static void _sgm37603a_led_set_brightness(
			struct sgm37603a *lp,
			enum led_brightness brightness)
{
	struct led_classdev *bl = &(lp->led_data->bl);
	enum sgm37603a_brightness_ctrl_mode mode = lp->mode;
	int gpio_en = lp->gpio_en;
	u8 last_brightness = lp->last_brightness;

	dev_info(lp->dev, "%s (last_br=%d new_br=%d)\n", __func__,
				last_brightness, brightness);

	if (last_brightness == brightness) {
		dev_dbg(lp->dev, "BL nothing to do\n");
		/*nothing to do*/
		goto exit;
	}

	/* power down if required */
	if ((last_brightness) &&  (!brightness)) {
		sgm37603a_bl_off(lp);
		/* Power Down */
		if (gpio_en  >= 0)
			gpio_set_value(gpio_en, 0);

		/*backlight is off, so wake up lcd disable */
		dev_info(lp->dev, "BL power down\n");

		/*For suspend power sequence: T6 > 200 ms */
		msleep(200);
	} else {
		if (gpio_en  >= 0)
			gpio_set_value(gpio_en, 1);

		sgm37603a_bl_on(lp);
	}

	if (mode == REGISTER_BASED) {
		u32 val = brightness;

		if (lp->brightness_limit)
			val = brightness * lp->brightness_limit /
				lp->max_brightness;

		if (val == 1) {		/* min brightness: 1 */
			val = 0x4;
		} else if (val > 1) {
			val = (val - 1) * 12;	/* 2980 / 255 == 11.6 */
			if (val > 2980)		/* 0xba4 */
				val = 2980;
		}

		sgm37603a_write_byte(lp, lp->cfg->reg_devicectrl, 0x00);
		sgm37603a_write_byte(lp, lp->cfg->reg_brightness1, val & 0xF);
		sgm37603a_write_byte(lp, lp->cfg->reg_brightness2, (val >> 4) & 0xFF);
	}

	lp->last_brightness =  brightness;
	bl->brightness      =  brightness;
exit:
	return;
}

void sgm37603a_led_set_brightness(
		struct led_classdev *led_cdev,
		enum led_brightness brightness)
{
	struct sgm37603a_led_data *led_data = container_of(led_cdev,
			struct sgm37603a_led_data, bl);

	struct sgm37603a *lp = led_data->lp;

	mutex_lock(&(lp->bl_update_lock));
	_sgm37603a_led_set_brightness(lp, brightness);
	mutex_unlock(&(lp->bl_update_lock));
}
EXPORT_SYMBOL(sgm37603a_led_set_brightness);

static enum led_brightness _sgm37603a_led_get_brightness(struct sgm37603a *lp)
{
	struct led_classdev *bl = &(lp->led_data->bl);

	return bl->brightness;
}

enum led_brightness sgm37603a_led_get_brightness(struct led_classdev *led_cdev)
{
	struct sgm37603a_led_data *led_data = container_of(led_cdev,
			struct sgm37603a_led_data, bl);
	struct sgm37603a *lp = led_data->lp;
	enum led_brightness ret = 0;

	mutex_lock(&(lp->bl_update_lock));

	ret = _sgm37603a_led_get_brightness(lp);

	mutex_unlock(&(lp->bl_update_lock));

	return ret;
}
EXPORT_SYMBOL(sgm37603a_led_get_brightness);

static int sgm37603a_backlight_register(struct sgm37603a *lp)
{
	int ret;

	struct sgm37603a_led_data *led_data;
	struct led_classdev *bl;

	char *name = DEFAULT_BL_NAME;

	led_data = kzalloc(sizeof(struct sgm37603a_led_data), GFP_KERNEL);

	if (led_data == NULL) {
		ret = -ENOMEM;
		return ret;
	}

	bl = &(led_data->bl);

	bl->name = name;
	bl->max_brightness = MAX_BRIGHTNESS;
	bl->brightness_set = sgm37603a_led_set_brightness;
	bl->brightness_get = sgm37603a_led_get_brightness;
	led_data->lp = lp;
	lp->led_data = led_data;

	if (lp->initial_brightness > bl->max_brightness)
		lp->initial_brightness = bl->max_brightness;

	bl->brightness = lp->initial_brightness;

	ret = led_classdev_register(lp->dev, bl);
	if (ret < 0) {
		lp->led_data = NULL;
		kfree(led_data);
		return ret;
	}

	return 0;
}

static void sgm37603a_backlight_unregister(struct sgm37603a *lp)
{
	if (lp->led_data) {
		led_classdev_unregister(&(lp->led_data->bl));
		kfree(lp->led_data);
		lp->led_data = NULL;
	}
}

static ssize_t sgm37603a_get_chip_id(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sgm37603a *lp = dev_get_drvdata(dev);

	return scnprintf(buf, BUF_SIZE, "%s\n", lp->chipname);
}

static ssize_t sgm37603a_get_bl_ctl_mode(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct sgm37603a *lp = dev_get_drvdata(dev);
	enum sgm37603a_brightness_ctrl_mode mode = lp->mode;
	char *strmode = NULL;

	if (mode == REGISTER_BASED)
		strmode = "register based";

	return scnprintf(buf, BUF_SIZE, "%s\n", strmode);
}

static DEVICE_ATTR(chip_id, 0444, sgm37603a_get_chip_id, NULL);
static DEVICE_ATTR(bl_ctl_mode, 0444, sgm37603a_get_bl_ctl_mode, NULL);

static struct attribute *sgm37603a_attributes[] = {
	&dev_attr_chip_id.attr,
	&dev_attr_bl_ctl_mode.attr,
	NULL,
};

static const struct attribute_group sgm37603a_attr_group = {
	.attrs = sgm37603a_attributes,
};

static struct sgm37603a_rom_data sgm37603a_eeprom[6];
#if 0
{	{0x14, 0xCF}, /* 4V OV, 4 LED string enabled */
	{0x13, 0x01}, /* Boost frequency @1MHz */
	/* full scale 23 mA, but user isn't allowed to set full scale */
	{0x11, 0x06},
	{0x12, 0x2B}, /* 19.5 kHz */
	{0x15, 0x42}, /* light smoothing, 100 ms */
	{0x16, 0xA0}, /* Max boost voltage 25V, enable adaptive voltage */
};
#endif

static int sgm37603a_probe(struct i2c_client *cl, const struct i2c_device_id *id)
{
	struct sgm37603a *lp;
	enum sgm37603a_brightness_ctrl_mode mode;
	int ret;

	pr_info("sgm37603a_probe +\n");
	if (!i2c_check_functionality(cl->adapter, I2C_FUNC_SMBUS_I2C_BLOCK))
		return -EIO;

	lp = devm_kzalloc(&cl->dev, sizeof(struct sgm37603a), GFP_KERNEL);
	if (!lp)
		return -ENOMEM;

	lp->client = cl;
	lp->dev = &cl->dev;
	/* lp->chipname = id->name; */
	/* lp->chip_id = id->driver_data;*/
	lp->chipname = "sgm37603a_led";
	lp->chip_id = SGM37603A;
	i2c_set_clientdata(cl, lp);
	mutex_init(&(lp->bl_update_lock));

	/*
	 * FIXME : Will read from device tree later
	 */
	lp->mode = REGISTER_BASED;
	lp->initial_brightness = 100;
	lp->device_control = SGM37603A_DEVICE_CTRL;
	lp->max_brightness = 255;
	lp->brightness_limit = 0;
	lp->load_new_rom_data = 1;
	lp->size_program = ARRAY_SIZE(sgm37603a_eeprom);
	lp->rom_data = sgm37603a_eeprom;
	if (of_property_read_u8_array(cl->dev.of_node,
		"eeprom_data", (u8 *)sgm37603a_eeprom, 12))
		lp->load_new_rom_data = 0;

	lp->gpio_en = of_get_named_gpio_flags(
		cl->dev.of_node, "gpio_en", 0, NULL);

	mode = lp->mode;

	if (lp->gpio_en >= 0) {
		ret = gpio_request(lp->gpio_en, "backlight_sgm37603a_hwen_gpio");
		if (ret != 0) {
			pr_err("backlight gpio request failed\n");
			 /* serve as a flag as well */
			lp->gpio_en = -EINVAL;
		}
	}

	ret = sgm37603a_configure(lp);
	if (ret) {
		dev_err(lp->dev, "device config err: %d\n", ret);
		goto err_dev;
	}

	ret = sgm37603a_backlight_register(lp);
	if (ret) {
		dev_err(lp->dev,
			" failed to register backlight. err: %d\n", ret);
		goto err_dev;
	}

	ret = sysfs_create_group(&lp->dev->kobj, &sgm37603a_attr_group);
	if (ret) {
		dev_err(lp->dev, " failed to register sysfs. err: %d\n", ret);
		goto err_sysfs;
	}

	sgm37603a_led_set_brightness(&(lp->led_data->bl), lp->initial_brightness);

	pr_info("sgm37603a_probe -\n");
	return 0;

err_sysfs:
	sgm37603a_backlight_unregister(lp);
err_dev:
	mutex_destroy(&(lp->bl_update_lock));
	return ret;
}

static int sgm37603a_remove(struct i2c_client *cl)
{
	struct sgm37603a *lp = i2c_get_clientdata(cl);

	mutex_lock(&(lp->bl_update_lock));

	_sgm37603a_led_set_brightness(lp, 0);
	sysfs_remove_group(&lp->dev->kobj, &sgm37603a_attr_group);
	sgm37603a_backlight_unregister(lp);
	mutex_destroy(&(lp->bl_update_lock));

	return 0;
}

#ifdef CONFIG_PM
static void sgm37603a_shutdown(struct i2c_client *cl)
{
	struct sgm37603a *lp = i2c_get_clientdata(cl);

	mutex_lock(&(lp->bl_update_lock));
	_sgm37603a_led_set_brightness(lp, 0);
	pr_info("%s shutdown\n", __func__);
	mutex_unlock(&(lp->bl_update_lock));

}
#endif

static const struct i2c_device_id sgm37603a_ids[] = {
	{"sgm37603a_led", SGM37603A},
	{ }
};
MODULE_DEVICE_TABLE(i2c, sgm37603a_ids);

static const struct of_device_id sgm37603a_id[] = {
	{.compatible = "sgmicro,sgm37603a-led"},
	{},
};
MODULE_DEVICE_TABLE(OF, sgm37603a_id);

static struct i2c_driver sgm37603a_driver = {
	.driver = {
		.name = "sgm37603a_led",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(sgm37603a_id),
	},
	.probe = sgm37603a_probe,
	.remove = sgm37603a_remove,
	.shutdown = sgm37603a_shutdown,
	.id_table = sgm37603a_ids,
};

module_i2c_driver(sgm37603a_driver);

MODULE_DESCRIPTION("SGMICRO SGM37603A Backlight driver");
MODULE_AUTHOR("Xi Chen<xixi.chen@mediatek.com>");
MODULE_LICENSE("GPL");


