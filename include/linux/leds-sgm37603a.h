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

#ifndef _LEDS_SGM37603A_H
#define _LEDS_SGM37603A_H

#include <linux/leds.h>

#define BL_CTL_SHFT	(0)
#define BRT_MODE_SHFT	(1)
#define BRT_MODE_MASK	(0x06)

/* Enable backlight. Only valid when BRT_MODE=10(I2C only) */
#define ENABLE_BL	(1)
#define DISABLE_BL	(0)

#define I2C_CONFIG(id)	id ## _I2C_CONFIG
#define PWM_CONFIG(id)	id ## _PWM_CONFIG

/* DEVICE CONTROL register - SGM37603A */
#define SGM37603A_PWM_CONFIG	(SGM37603A_PWM_ONLY << BRT_MODE_SHFT)
#define SGM37603A_I2C_CONFIG	((ENABLE_BL << BL_CTL_SHFT) | \
				(SGM37603A_I2C_ONLY << BRT_MODE_SHFT))

/* CONFIG register - SGM37603 */
#define SGM37603_PWM_STANDBY	BIT(7)
#define SGM37603_PWM_FILTER	BIT(6)
/* use it if EPROMs should be reset
  * when the backlight turns on
  */
#define SGM37603_RELOAD_EPROM	BIT(3)
#define SGM37603_DISABLE_LEDS	BIT(2)
#define SGM37603_PWM_CONFIG	SGM37603_PWM_ONLY
#define SGM37603_I2C_CONFIG	SGM37603_I2C_ONLY
#define SGM37603_COMB1_CONFIG	SGM37603_COMBINED1
#define SGM37603_COMB2_CONFIG	SGM37603_COMBINED2

enum sgm37603a_chip_id {
	SGM37603A,
};

enum sgm37603a_brightness_ctrl_mode {
	PWM_BASED = 1,
	REGISTER_BASED,
};

enum sgm37603a_brightness_source {
	SGM37603_PWM_ONLY,
	SGM37603_I2C_ONLY,
	SGM37603_COMBINED1,	/* pwm + i2c after the shaper block */
	SGM37603_COMBINED2,	/* pwm + i2c before the shaper block */
};

struct sgm37603a_pwm_data {
	void (*pwm_set_intensity)(int brightness, int max_brightness);
	int (*pwm_get_intensity)(int max_brightness);
};

struct sgm37603a_rom_data {
	u8 addr;
	u8 val;
};

/**
 * struct sgm37603a_platform_data
 * @name : Backlight driver name. If it is not defined, default name is set.
 * @mode : brightness control by pwm or sgm37603a register
 * @device_control : value of DEVICE CONTROL register
 * @initial_brightness : initial value of backlight brightness
 * @pwm_data : platform specific pwm generation functions.
		Only valid when mode is PWM_BASED.
 * @load_new_rom_data :
	0 : use default configuration data
	1 : update values of eeprom or eprom registers on loading driver
 * @size_program : total size of sgm37603a_rom_data
 * @rom_data : list of new eeprom/eprom registers
 * @gpio_en : num of GPIO driving enable pin
 */
struct sgm37603a_platform_data {
	char *name;
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

void sgm37603a_led_set_brightness(struct led_classdev *led_cdev,
			       enum led_brightness brightness);
enum led_brightness sgm37603a_led_get_brightness(struct led_classdev *led_cdev);

#endif


