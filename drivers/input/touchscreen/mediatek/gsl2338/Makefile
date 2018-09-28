ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/hwmon/include
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/alsps/inc
ccflags-y += -I$(srctree)/drivers/misc/mediatek/sensors-1.0/alsps/

obj-y	+=  gsl_ts_driver.o

obj-y += gsl_point_id.o
ifeq ($(CONFIG_ARM64),y)
$(obj)/gsl_point_id.o: $(srctree)/$(obj)/gsl_point_id_arm64_o
	cp $(srctree)/$(obj)/gsl_point_id_arm64_o $(obj)/gsl_point_id.o
else ifeq ($(CONFIG_ARM),y)
$(obj)/gsl_point_id.o: $(srctree)/$(obj)/gsl_point_id_arm32_o
	cp $(srctree)/$(obj)/gsl_point_id_arm32_o $(obj)/gsl_point_id.o
endif
