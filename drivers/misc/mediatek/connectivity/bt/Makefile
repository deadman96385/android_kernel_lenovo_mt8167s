#########################################################
# Bluetooth character device driver
#########################################################

############ 7668 BT sdio Driver ########################
ifneq ($(filter "MT7668",$(CONFIG_MTK_COMBO_CHIP)),)
obj-m += btmtksdio.o
btmtksdio-objs     := ./mt76xx/btmtk_sdio.o ./mt76xx/btmtk_main.o
else
ccflags-y += -D CREATE_NODE_DYNAMIC=1

ccflags-y += \
	-I$(src)/../common/common_main/include \
	-I$(src)/../common/common_main/linux/include

obj-$(CONFIG_MTK_COMBO_BT) += ./mt66xx/stp_chrdev_bt.o
endif
