subdir-ccflags-y += -Werror
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include
# In case the platform does NOT support this type of sensors
obj-y   +=  mtk_tpd.o
obj-y   +=  tpd_button.o
obj-y   +=  tpd_calibrate.o
obj-y   +=  tpd_debug.o
obj-y   +=  tpd_default.o
obj-y   +=  tpd_init.o
obj-y   +=  tpd_misc.o
obj-y   +=  tpd_setting.o

obj-$(CONFIG_TOUCHSCREEN_MTK_GT1151)	+=  GT1151/
obj-$(CONFIG_TOUCHSCREEN_MTK_GT911)	+=  GT911/
obj-$(CONFIG_TOUCHSCREEN_MTK_GT9157)	+=  GT9157/
obj-$(CONFIG_TOUCHSCREEN_MTK_GSLX680)	+=  gslX680/
obj-$(CONFIG_TOUCHSCREEN_MTK_FT3427)	+=  FT3427/
obj-$(CONFIG_TOUCHSCREEN_FT5X26)	+=  FT5X26/
obj-$(CONFIG_TOUCHSCREEN_MTK_FT6336)	+=  FT6336/
obj-$(CONFIG_TOUCHSCREEN_MTK_GSL2338)	+=  gsl2338/
obj-$(CONFIG_TOUCHSCREEN_MTK_GT9XX) += gt9xx_mtk/
