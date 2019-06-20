
# Linux driver folder
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/FT6336/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/
ccflags-y += -I$(srctree)/drivers/base
ccflags-y += -I$(srctree)/drivers/misc/mediatek/hwmon/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/alsps/inc/

obj-$(CONFIG_TOUCHSCREEN_MTK_FT6336)    +=  focaltech_ex_fun.o
obj-$(CONFIG_TOUCHSCREEN_MTK_FT6336)    +=  focaltech_flash.o
obj-$(CONFIG_TOUCHSCREEN_MTK_FT6336)    +=  focaltech_gesture.o
obj-$(CONFIG_TOUCHSCREEN_MTK_FT6336)    +=  focaltech_core.o
obj-$(CONFIG_TOUCHSCREEN_MTK_FT6336)    +=  focaltech_esdcheck.o
obj-$(CONFIG_TOUCHSCREEN_MTK_FT6336)    +=  focaltech_ex_mode.o
obj-$(CONFIG_TOUCHSCREEN_MTK_FT6336)    +=  focaltech_proximity.o
obj-$(CONFIG_TOUCHSCREEN_MTK_FT6336)    +=  focaltech_i2c.o
obj-$(CONFIG_TOUCHSCREEN_MTK_FT6336)    +=  focaltech_point_report_check.o

obj-$(CONFIG_TOUCHSCREEN_MTK_FT6336) +=     focaltech_flash/
