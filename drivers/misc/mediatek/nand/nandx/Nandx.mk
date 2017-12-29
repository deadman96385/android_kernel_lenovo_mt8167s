#
# Copyright (C) 2017 MediaTek Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See http://www.gnu.org/licenses/gpl-2.0.html for more details.
#

# Nandx Preloader Makefile
ifeq ($(CONFIG_NANDX_MK_PRELOADER), y)
NANDX_PATH := nandx
NANDX_OBJ := MOD_SRC
NANDX_OS := preloader
NANDX_PREFIX :=
NANDX_POSTFIX := %.c
endif

# Nandx LK Makefile
ifeq ($(CONFIG_NANDX_MK_LK), y)
NANDX_OBJ := OBJS
NANDX_OS := lk
NANDX_PREFIX := $(LOCAL_DIR)/
NANDX_POSTFIX := %.o
NANDX_PATH := $(NANDX_PREFIX)nandx
endif

# Nandx Kernel Makefile
ifeq ($(CONFIG_NANDX_MK_KERNEL), y)
NANDX_PATH := $(srctree)/drivers/misc/mediatek/nand/nandx
ccflags-y += -I$(NANDX_PATH)/include/kernel
ccflags-y += -I$(NANDX_PATH)/include/internal
ccflags-y += -I$(NANDX_PATH)/driver/kernel
NANDX_OBJ := obj-y
NANDX_OS := kernel
NANDX_PREFIX :=
NANDX_POSTFIX := %.o
endif

NANDX_CORE := $(NANDX_PREFIX)nandx/core
NANDX_DRIVER := $(NANDX_PREFIX)nandx/driver
NANDX_CUSTOM := $(NANDX_PREFIX)nandx/driver/$(NANDX_OS)

NANDX_SRC :=
include $(NANDX_PATH)/core/Nandx.mk
$(NANDX_OBJ) += $(patsubst %.c, $(NANDX_CORE)/$(NANDX_POSTFIX), $(NANDX_SRC))

NANDX_SRC :=
include $(NANDX_PATH)/driver/Nandx.mk
$(NANDX_OBJ) += $(patsubst %.c, $(NANDX_DRIVER)/$(NANDX_POSTFIX), $(NANDX_SRC))

NANDX_SRC :=
include $(NANDX_PATH)/driver/$(NANDX_OS)/Nandx.mk
$(NANDX_OBJ) += $(patsubst %.c, $(NANDX_CUSTOM)/$(NANDX_POSTFIX), $(NANDX_SRC))
