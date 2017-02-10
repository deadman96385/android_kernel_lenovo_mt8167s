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

ccflags-y += -I$(srctree)/drivers/misc/mediatek/base/power/mt8167
obj-$(CONFIG_MACH_MT8167) += mt8167-cpufreq.o
obj-$(CONFIG_MACH_MT8167) += mt_static_power.o
obj-$(CONFIG_MACH_MT8167) += mt_power_throttle.o

obj-$(CONFIG_MACH_MT8167) += mt_spm.o
obj-$(CONFIG_MACH_MT8167) += mt_spm_internal.o
obj-$(CONFIG_MACH_MT8167) += mt_sleep.o
obj-$(CONFIG_MACH_MT8167) += mt_spm_sleep.o
obj-$(CONFIG_MACH_MT8167) += mt_spm_dpidle.o
obj-$(CONFIG_MACH_MT8167) += mt_spm_fs.o


obj-y += mt_cpuidle.o

obj-y += cpu_dormant.o
