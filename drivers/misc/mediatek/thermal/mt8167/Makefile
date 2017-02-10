#
# Copyright (C) 2015 MediaTek Inc.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#

ccflags-y  += -I$(THERMAL_CHIP_DRIVER_DIR)/inc
ccflags-y  += -I$(srctree)/drivers/misc/mediatek/base/power/$(MTK_PLATFORM)
ccflags-y  += -I$(srctree)/drivers/misc/mediatek/gpu/hal/
ccflags-y  += -I$(srctree)/drivers/misc/mediatek/thermal/fakeHeader/
obj-  := dummy.o

ifeq ($(CONFIG_THERMAL), y)
else
    obj-y += mtk_thermal_dummy.o
endif

obj-$(CONFIG_THERMAL) += mtk_thermal_platform.o
#obj-$(CONFIG_THERMAL) += mtk_ts_pa_thput.o
#obj-$(CONFIG_THERMAL) += mtk_cooler_bcct.o

ifneq ($(CONFIG_MTK_COMBO_WIFI),)
    obj-$(CONFIG_THERMAL) += mtk_ts_wmt.o
endif

# Only included in non-EVB projects
ifneq ($(CONFIG_POWER_EXT), y)
    obj-$(CONFIG_THERMAL) += mtk_cooler_bcct.o
endif

obj-$(CONFIG_THERMAL) += mtk_ts_bts.o
#obj-$(CONFIG_THERMAL) += mtk_ts_btsmdpa.o
obj-$(CONFIG_THERMAL) += mtk_ts_cpu.o
#obj-$(CONFIG_THERMAL) += mtk_ts_abb.o
#obj-$(CONFIG_THERMAL) += mtk_ts_all_ts.o
#obj-$(CONFIG_THERMAL) += mtk_ts_pa.o
#obj-$(CONFIG_THERMAL) += mtk_ts_battery.o
#obj-$(CONFIG_THERMAL) += mtk_ts_pmic6323.o

#obj-$(CONFIG_THERMAL) += tm_nth.o


