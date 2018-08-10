#########################################################
# Bluetooth character device driver
#########################################################

############ 7668 BT USB Driver ########################
ifneq ($(filter "MT7668",$(CONFIG_MTK_COMBO_CHIP)),)
obj-m += btmtk_usb.o
btmtk_usb-objs     := btmtk_usb_main.o btmtk_usb_fifo.o
endif
