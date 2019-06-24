/*
 *  Copyright (c) 2016,2017 MediaTek Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#include <linux/version.h>
#include <linux/firmware.h>
#include <linux/slab.h>

#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>
#include <linux/module.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/device.h>

/* Define for proce node */
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include "btmtk_config.h"
#include "btmtk_define.h"
#include "btmtk_drv.h"
#include "btmtk_sdio.h"

#if SUPPORT_EINT
/* Used for WoBLE on EINT */
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#endif


static dev_t g_devIDfwlog;
static struct class *pBTClass;
static struct device *pBTDev;
struct device *pBTDevfwlog;
static wait_queue_head_t inq;
static wait_queue_head_t fw_log_inq;
static struct fasync_struct *fasync;

static int need_reset_stack;
static int need_reopen;
static int need_set_i2s;
static int wlan_remove_done;

static u8 user_rmmod;

struct completion g_done;
unsigned char probe_counter;
unsigned char current_fwdump_file_number;
struct btmtk_private *g_priv;
#define STR_COREDUMP_END "coredump end\n\n"
const u8 READ_ADDRESS_EVENT[] = { 0x0e, 0x0a, 0x01, 0x09, 0x10, 0x00 };

static struct ring_buffer metabuffer;
static struct ring_buffer fwlog_metabuffer;
u8 probe_ready;
/* record firmware version */
static char fw_version_str[FW_VERSION_BUF_SIZE];
static struct proc_dir_entry *g_proc_dir;

/** read_write for proc */
static int btmtk_proc_show(struct seq_file *m, void *v);
static int btmtk_proc_open(struct inode *inode, struct  file *file);
static void btmtk_proc_create_new_entry(void);
static int btmtk_sdio_trigger_fw_assert(void);
#if SUPPORT_EINT
static int btmtk_sdio_RegisterBTIrq(struct btmtk_sdio_card *data);
static int btmtk_sdio_woble_input_init(struct btmtk_sdio_card *data);
#endif

static char fw_dump_file_name[FW_DUMP_FILE_NAME_SIZE] = {0};
static char event_need_compare[EVENT_COMPARE_SIZE] = {0};
static char event_need_compare_len;
static char event_compare_status;
/*add special header in the beginning of even, stack won't recognize these event*/


/* timer for coredump end */
struct task_struct	*wait_dump_complete_tsk;
struct task_struct	*wait_wlan_remove_tsk;
int wlan_status;

int fw_is_doing_coredump;
int fw_is_coredump_end_packet;
static int print_dump_data_counter;
#if SAVE_FW_DUMP_IN_KERNEL
	static struct file *fw_dump_file;
#else
	static int fw_dump_file;
#endif

const struct file_operations BT_proc_fops = {
	.open = btmtk_proc_open,
	.read = seq_read,
	.release = single_release,
};

static const struct btmtk_sdio_card_reg btmtk_reg_6630 = {
	.cfg = 0x03,
	.host_int_mask = 0x04,
	.host_intstatus = 0x05,
	.card_status = 0x20,
	.sq_read_base_addr_a0 = 0x10,
	.sq_read_base_addr_a1 = 0x11,
	.card_fw_status0 = 0x40,
	.card_fw_status1 = 0x41,
	.card_rx_len = 0x42,
	.card_rx_unit = 0x43,
	.io_port_0 = 0x00,
	.io_port_1 = 0x01,
	.io_port_2 = 0x02,
	.int_read_to_clear = false,
	.func_num = 2,
	.chip_id = 0x6630,
};

static const struct btmtk_sdio_card_reg btmtk_reg_6632 = {
	.cfg = 0x03,
	.host_int_mask = 0x04,
	.host_intstatus = 0x05,
	.card_status = 0x20,
	.sq_read_base_addr_a0 = 0x10,
	.sq_read_base_addr_a1 = 0x11,
	.card_fw_status0 = 0x40,
	.card_fw_status1 = 0x41,
	.card_rx_len = 0x42,
	.card_rx_unit = 0x43,
	.io_port_0 = 0x00,
	.io_port_1 = 0x01,
	.io_port_2 = 0x02,
	.int_read_to_clear = false,
	.func_num = 2,
	.chip_id = 0x6632,
};

static const struct btmtk_sdio_card_reg btmtk_reg_7668 = {
	.cfg = 0x03,
	.host_int_mask = 0x04,
	.host_intstatus = 0x05,
	.card_status = 0x20,
	.sq_read_base_addr_a0 = 0x10,
	.sq_read_base_addr_a1 = 0x11,
	.card_fw_status0 = 0x40,
	.card_fw_status1 = 0x41,
	.card_rx_len = 0x42,
	.card_rx_unit = 0x43,
	.io_port_0 = 0x00,
	.io_port_1 = 0x01,
	.io_port_2 = 0x02,
	.int_read_to_clear = false,
	.func_num = 2,
	.chip_id = 0x7668,
};

static const struct btmtk_sdio_card_reg btmtk_reg_7666 = {
	.cfg = 0x03,
	.host_int_mask = 0x04,
	.host_intstatus = 0x05,
	.card_status = 0x20,
	.sq_read_base_addr_a0 = 0x10,
	.sq_read_base_addr_a1 = 0x11,
	.card_fw_status0 = 0x40,
	.card_fw_status1 = 0x41,
	.card_rx_len = 0x42,
	.card_rx_unit = 0x43,
	.io_port_0 = 0x00,
	.io_port_1 = 0x01,
	.io_port_2 = 0x02,
	.int_read_to_clear = false,
	.func_num = 2,
	.chip_id = 0x7666,
};

static const struct btmtk_sdio_device btmtk_sdio_6630 = {
	.helper		= "mtmk/sd8688_helper.bin",
	.firmware	= "mt6632_patch_e1_hdr.bin",
	.reg		= &btmtk_reg_6630,
	.support_pscan_win_report = false,
	.sd_blksz_fw_dl = 64,
	.supports_fw_dump = false,
};

static const struct btmtk_sdio_device btmtk_sdio_6632 = {
	.helper     = "mtmk/sd8688_helper.bin",
	.firmware   = "mt6632_patch_e1_hdr.bin",
	.reg        = &btmtk_reg_6632,
	.support_pscan_win_report = false,
	.sd_blksz_fw_dl = 64,
	.supports_fw_dump = false,
};

static const struct btmtk_sdio_device btmtk_sdio_7668 = {
	.helper		= "mtmk/sd8688_helper.bin",
#if CFG_THREE_IN_ONE_FIRMWARE
	.firmware	= "MT7668_FW",
#else
	.firmware	= "mt7668_patch_e1_hdr.bin",
	.firmware1	= "mt7668_patch_e2_hdr.bin",
#endif
	.reg		= &btmtk_reg_7668,
	.support_pscan_win_report = false,
	.sd_blksz_fw_dl = 64,
	.supports_fw_dump = false,
};

static const struct btmtk_sdio_device btmtk_sdio_7666 = {
	.helper		= "mtmk/sd8688_helper.bin",
	.firmware	= "mt7668_patch_e1_hdr.bin",
	.reg		= &btmtk_reg_7666,
	.support_pscan_win_report = false,
	.sd_blksz_fw_dl = 64,
	.supports_fw_dump = false,
};

unsigned char *txbuf;
static unsigned char *rxbuf;
static unsigned char *userbuf;
static unsigned char *userbuf_fwlog;
static u32 rx_length;
static struct btmtk_sdio_card *g_card;

#define SDIO_VENDOR_ID_MEDIATEK 0x037A
static const struct sdio_device_id btmtk_sdio_ids[] = {
	/* Mediatek SD8688 Bluetooth device */
	{ SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, 0x6630),
			.driver_data = (unsigned long) &btmtk_sdio_6630 },

	{ SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, 0x6632),
			.driver_data = (unsigned long) &btmtk_sdio_6632 },

	{ SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, 0x7668),
			.driver_data = (unsigned long) &btmtk_sdio_7668 },

	{ SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, 0x7666),
			.driver_data = (unsigned long) &btmtk_sdio_7666 },

	{ }	/* Terminating entry */
};
MODULE_DEVICE_TABLE(sdio, btmtk_sdio_ids);

static int btmtk_clean_queue(void);
static void btmtk_sdio_do_reset_or_wait_wlan_remove_done(void);
static int btmtk_skb_enq(void *src, u32 len, struct sk_buff_head *queue,
	struct _OSAL_UNSLEEPABLE_LOCK_ *spin_lock);

void btmtk_sdio_stop_wait_wlan_remove_tsk(void)
{
	if (wait_wlan_remove_tsk == NULL)
		pr_info("%s wait_wlan_remove_tsk is NULL\n", __func__);
	else if (IS_ERR(wait_wlan_remove_tsk))
		pr_info("%s wait_wlan_remove_tsk is error\n", __func__);
	else {
		pr_info("%s call kthread_stop wait_wlan_remove_tsk\n",
			__func__);
		kthread_stop(wait_wlan_remove_tsk);
		wait_wlan_remove_tsk = NULL;
	}
}

int btmtk_sdio_notify_wlan_remove_start(void)
{
	/* notify_wlan_remove_start */
	int ret = 0;
	typedef void (*pnotify_wlan_remove_start) (int reserved);
	char *notify_wlan_remove_start_func_name = "notify_wlan_remove_start";
	pnotify_wlan_remove_start pnotify_wlan_remove_start_func =
		(pnotify_wlan_remove_start)kallsyms_lookup_name(notify_wlan_remove_start_func_name);

	pr_info("%s: wlan_status %d\n", __func__, wlan_status);
	if (wlan_status == WLAN_STATUS_CALL_REMOVE_START) {
		/* do notify before, just return */
		return ret;
	}

	btmtk_sdio_stop_wait_wlan_remove_tsk();
	/* void notify_wlan_remove_start(void) */
	if (pnotify_wlan_remove_start_func) {
		pr_info("%s: do notify %s\n", __func__, notify_wlan_remove_start_func_name);
		wlan_remove_done = 0;
		pnotify_wlan_remove_start_func(1);
		wlan_status = WLAN_STATUS_CALL_REMOVE_START;
	} else {
		ret = -1;
		pr_err("%s: do not get %s\n", __func__, notify_wlan_remove_start_func_name);
		wlan_status = WLAN_STATUS_IS_NOT_LOAD;
		wlan_remove_done = 1;
	}
	return ret;
}

/*============================================================================*/
/* Interface Functions : timer for coredump inform wifi */
/*============================================================================*/
static void btmtk_sdio_wakeup_mainthread_do_reset(void)
{
	if (g_priv) {
		g_priv->btmtk_dev.reset_dongle = 1;
		pr_info("%s: set reset_dongle %d", __func__, g_priv->btmtk_dev.reset_dongle);
		wake_up_interruptible(&g_priv->main_thread.wait_q);
	} else
		pr_err("%s: g_priv is NULL", __func__);
}

static int btmtk_sdio_wait_wlan_remove_thread(void *ptr)
{
	int  i;

	pr_info("%s: begin", __func__);
	if (g_priv == NULL) {
		pr_err("%s: g_priv is NULL, return", __func__);
		return 0;
	}

	g_priv->btmtk_dev.reset_progress = 1;
	for (i = 0; i < 30; i++) {
		if ((wait_wlan_remove_tsk && kthread_should_stop()) || wlan_remove_done) {
			pr_warn("%s: break wlan_remove_done %d\n", __func__, wlan_remove_done);
			break;
		}
		msleep(500);
	}

	btmtk_sdio_wakeup_mainthread_do_reset();

	while (!kthread_should_stop()) {
		pr_info("%s: no one call %s stop", __func__, __func__);
		msleep(500);
	}

	pr_info("%s: end", __func__);
	return 0;
}
static void btmtk_sdio_start_reset_dongle_progress(void)
{
#if defined(MTK_KERNEL_DEBUG)
	pr_warn("%s: debug mode do not do reset", __func__);
#else
	pr_warn("%s: user mode do reset", __func__);
	btmtk_sdio_notify_wlan_remove_start();
	btmtk_sdio_do_reset_or_wait_wlan_remove_done();
#endif
}

/*============================================================================*/
/* Interface Functions : timer for uncomplete coredump */
/*============================================================================*/
static void btmtk_sdio_do_reset_or_wait_wlan_remove_done(void)
{
	pr_info("%s: wlan_remove_done %d\n", __func__, wlan_remove_done);
	if (wlan_remove_done || (wlan_status == WLAN_STATUS_IS_NOT_LOAD))
		/* wifi inform bt already, reset chip */
		btmtk_sdio_wakeup_mainthread_do_reset();
	else {
		/* create thread wait wifi inform bt */
		pr_info("%s: create btmtk_sdio_wait_wlan_remove_thread\n", __func__);
		wait_wlan_remove_tsk = kthread_run(
			btmtk_sdio_wait_wlan_remove_thread, NULL,
			"btmtk_sdio_wait_wlan_remove_thread");
		msleep(50);
		if (wait_wlan_remove_tsk == NULL)
			pr_err("%s: btmtk_sdio_wait_wlan_remove_thread create fail\n", __func__);
	}
}

static int btmtk_sdio_wait_dump_complete_thread(void *ptr)
{
	int  i;

	pr_info("%s: begin", __func__);
	for (i = 0; i < 60; i++) {
		if (wait_dump_complete_tsk && kthread_should_stop()) {
			pr_warn("%s: thread is stopped, break\n", __func__);
			break;
		}
		msleep(500);
	}

#if defined(MTK_KERNEL_DEBUG)
	pr_info("%s: debug mode don't do reset\n", __func__);
#else
	pr_info("%s: user mode call do reset\n", __func__);
	btmtk_sdio_do_reset_or_wait_wlan_remove_done();
#endif

	pr_info("%s: end", __func__);
	return 0;
}

static void btmtk_sdio_woble_free_setting_struct(struct woble_setting_struct *woble_struct, int count)
{
	int i = 0;

	for (i = 0; i < count; i++) {
		if (woble_struct[i].content) {
			pr_info("%s:kfree %d", __func__, i);
			kfree(woble_struct[i].content);
			woble_struct[i].content = NULL;
			woble_struct[i].length = 0;
		} else
			woble_struct[i].length = 0;
	}
}

static void btmtk_sdio_woble_free_setting(void)
{
	pr_info("%s begin", __func__);
	if (g_card == NULL) {
		pr_err("%s: g_data == NULL", __func__);
		return;
	}

	btmtk_sdio_woble_free_setting_struct(g_card->woble_setting_apcf, WOBLE_SETTING_COUNT);
	btmtk_sdio_woble_free_setting_struct(g_card->woble_setting_apcf_fill_mac, WOBLE_SETTING_COUNT);
	btmtk_sdio_woble_free_setting_struct(g_card->woble_setting_apcf_fill_mac_location, WOBLE_SETTING_COUNT);
	btmtk_sdio_woble_free_setting_struct(g_card->woble_setting_radio_off, WOBLE_SETTING_COUNT);
	btmtk_sdio_woble_free_setting_struct(g_card->woble_setting_radio_off_status_event, WOBLE_SETTING_COUNT);
	btmtk_sdio_woble_free_setting_struct(g_card->woble_setting_radio_off_comp_event, WOBLE_SETTING_COUNT);
	btmtk_sdio_woble_free_setting_struct(g_card->woble_setting_radio_on, WOBLE_SETTING_COUNT);
	btmtk_sdio_woble_free_setting_struct(g_card->woble_setting_radio_on_status_event, WOBLE_SETTING_COUNT);
	btmtk_sdio_woble_free_setting_struct(g_card->woble_setting_radio_on_comp_event, WOBLE_SETTING_COUNT);

	if (g_card->woble_setting_len) {
		kfree(g_card->woble_setting);
		g_card->woble_setting_len = 0;
		g_card->woble_setting = NULL;
	}
	pr_info("%s end", __func__);
}

static int btmtk_sdio_load_woble_block_setting(char *block_name, struct woble_setting_struct *save_content,
			int save_content_count, u8 *searchconetnt)
{
	int ret = 0;
	int i = 0;
	long parsing_result = 0;
	u8 *search_result = NULL;
	u8 *search_end = NULL;
	u8 search[32];
	u8 temp[128]; /* save for total hex number */
	u8 *next_number = NULL;
	u8 *next_block = NULL;
	u8 number[8];
	int temp_len;

	memset(search, 0, sizeof(search));
	memset(temp, 0, sizeof(temp));
	memset(number, 0, sizeof(number));

	/* search block name */
	for (i = 0; i < WOBLE_SETTING_COUNT; i++) {
		temp_len = 0;
		snprintf(search, sizeof(search), "%s%02d:", block_name, i); /* ex APCF01 */
		search_result = strstr(searchconetnt, search);
		if (search_result) {
			memset(temp, 0, sizeof(temp));
			temp_len = 0;
			search_result += strlen(search); /* move to first number */

			do {
				next_number = NULL;
				search_end = strstr(search_result, ",");
				if ((search_end - search_result) <= 0) {
					pr_info("%s can not find search end, break", __func__);
					break;
				}

				if ((search_end - search_result) > sizeof(number))
					break;

				memset(number, 0, sizeof(number));
				memcpy(number, search_result, search_end - search_result);

				if (number[0] == 0x20) /* space */
					ret = kstrtol(number + 1, 0, &parsing_result);
				else
					ret = kstrtol(number, 0, &parsing_result);

				if (ret == 0) {
					if (temp_len >= sizeof(temp)) {
						pr_err("%s: %s data over %zu", __func__, search, sizeof(temp));
						break;
					}
					temp[temp_len] = parsing_result;
					temp_len++;
					/* find next number */
					next_number = strstr(search_end, "0x");

					/* find next block */
					next_block = strstr(search_end, ":");
				} else {
					pr_debug("%s:kstrtol ret = %d, search %s", __func__, ret, search);
					break;
				}

				if (next_number == NULL) {
					pr_debug("%s: not find next apcf number temp_len %d, break, search %s",
						__func__, temp_len, search);
					break;
				}

				if ((next_number > next_block) && (next_block != 0)) {
					pr_debug("%s: find next apcf number is over to next block ", __func__);
					pr_debug("%s: temp_len %d, break, search %s",
						__func__, temp_len, search);
					break;
				}

				search_result = search_end + 1;
			} while (1);
		} else
			pr_debug("%s: %s is not found", __func__, search);

		if (temp_len) {
			pr_info("%s: %s found", __func__, search);
			pr_debug("%s: kzalloc i=%d temp_len=%d", __func__, i, temp_len);
			save_content[i].content = kzalloc(temp_len, GFP_KERNEL);
			memcpy(save_content[i].content, temp, temp_len);
			save_content[i].length = temp_len;
			pr_debug("%s:x  save_content[%d].length %d temp_len=%d",
				__func__, i, save_content[i].length, temp_len);
		}

	}
	return ret;
}

static int btmtk_sdio_load_woble_setting(u8 *image, char *bin_name,
					 struct device *dev, u32 *code_len, struct btmtk_sdio_card *data)
{
	int err;
	const struct firmware *fw_entry = NULL;

	pr_info("%s: begin woble_setting_file_name = %s", __func__, bin_name);
	err = request_firmware(&fw_entry, bin_name, dev);
	if (err != 0) {
		kfree(image);
		image = NULL;
		pr_err("%s: request_firmware function for woble setting  fail!! error code = %d", __func__, err);
		return err;
	}

	if (fw_entry) {
		image = kzalloc(fw_entry->size + 1, GFP_KERNEL); /* w:move to btmtk_usb_free_memory */
		pr_info("%s: woble_setting request_firmware size %zu success", __func__, fw_entry->size);
	} else {
		pr_err("%s: fw_entry is NULL request_firmware may fail!! error code = %d", __func__, err);
		return err;
	}
	if (image == NULL) {
		pr_err("%s: kzalloc size %zu failed!!", __func__, fw_entry->size);
		*code_len = 0;
		return err;
	}

	memcpy(image, fw_entry->data, fw_entry->size);
	image[fw_entry->size] = '\0';

	*code_len = fw_entry->size;
	pr_info("%s: code_len (%d) assign done", __func__, *code_len);

	err = btmtk_sdio_load_woble_block_setting("APCF",
			data->woble_setting_apcf, WOBLE_SETTING_COUNT, image);
	if (err)
		goto LOAD_END;

	err = btmtk_sdio_load_woble_block_setting("APCF_ADD_MAC",
			data->woble_setting_apcf_fill_mac, WOBLE_SETTING_COUNT, image);
	if (err)
		goto LOAD_END;

	err = btmtk_sdio_load_woble_block_setting("APCF_ADD_MAC_LOCATION",
			data->woble_setting_apcf_fill_mac_location, WOBLE_SETTING_COUNT, image);
	if (err)
		goto LOAD_END;

	err = btmtk_sdio_load_woble_block_setting("RADIOOFF",
			data->woble_setting_radio_off, WOBLE_SETTING_COUNT, image);
	if (err)
		goto LOAD_END;

	err = btmtk_sdio_load_woble_block_setting("RADIOOFF_STATUS_EVENT",
			data->woble_setting_radio_off_status_event, WOBLE_SETTING_COUNT, image);
	if (err)
		goto LOAD_END;

	err = btmtk_sdio_load_woble_block_setting("RADIOOFF_COMPLETE_EVENT",
			data->woble_setting_radio_off_comp_event, WOBLE_SETTING_COUNT, image);
	if (err)
		goto LOAD_END;

	err = btmtk_sdio_load_woble_block_setting("RADIOON",
			data->woble_setting_radio_on, WOBLE_SETTING_COUNT, image);
	if (err)
		goto LOAD_END;

	err = btmtk_sdio_load_woble_block_setting("RADIOON_STATUS_EVENT",
			data->woble_setting_radio_on_status_event, WOBLE_SETTING_COUNT, image);
	if (err)
		goto LOAD_END;

	err = btmtk_sdio_load_woble_block_setting("RADIOON_COMPLETE_EVENT",
			data->woble_setting_radio_on_comp_event, WOBLE_SETTING_COUNT, image);
	if (err)
		goto LOAD_END;

	err = btmtk_sdio_load_woble_block_setting("APCF_RESMUE",
		data->woble_setting_apcf_resume, WOBLE_SETTING_COUNT, image);
	if (err)
		goto LOAD_END;

	err = btmtk_sdio_load_woble_block_setting("APCF_COMPLETE_EVENT",
			data->woble_setting_apcf_resume, WOBLE_SETTING_COUNT, image);

LOAD_END:
	if (err)
		pr_info("%s: error return %d", __func__, err);

	return err;
}

static inline void btmtk_sdio_woble_wake_lock(struct btmtk_sdio_card *data)
{
#if (SUPPORT_UNIFY_WOBLE & SUPPORT_ANDROID)
	pr_info("%s:", __func__);
	wake_lock(&data->woble_wlock);
#endif
}

static inline void btmtk_sdio_woble_wake_unlock(struct btmtk_sdio_card *data)
{
#if (SUPPORT_UNIFY_WOBLE & SUPPORT_ANDROID)
	pr_info("%s:", __func__);
	wake_unlock(&data->woble_wlock);
#endif
}

u32 LOCK_UNSLEEPABLE_LOCK(struct _OSAL_UNSLEEPABLE_LOCK_ *pUSL)
{
	spin_lock_irqsave(&(pUSL->lock), pUSL->flag);
	return 0;
}

u32 UNLOCK_UNSLEEPABLE_LOCK(struct _OSAL_UNSLEEPABLE_LOCK_ *pUSL)
{
	spin_unlock_irqrestore(&(pUSL->lock), pUSL->flag);
	return 0;
}

static int btmtk_sdio_readb(u32 offset, u32 *val)
{
	u32 ret = 0;

	if (g_card->func == NULL) {
		pr_err("%s g_card->func is NULL\n", __func__);
		return -EIO;
	}
	sdio_claim_host(g_card->func);
	*val = sdio_readb(g_card->func, offset, &ret);
	sdio_release_host(g_card->func);
	return ret;
}

static int btmtk_sdio_writel(u32 offset, u32 val)
{
	u32 ret = 0;

	if (g_card->func == NULL) {
		pr_err("%s g_card->func is NULL\n", __func__);
		return -EIO;
	}
	sdio_claim_host(g_card->func);
	sdio_writel(g_card->func, val, offset, &ret);
	sdio_release_host(g_card->func);
	return ret;
}

static int btmtk_sdio_readl(u32 offset,  u32 *val)
{
	u32 ret = 0;

	if (g_card->func == NULL) {
		pr_err("g_card->func is NULL\n");
		return -EIO;
	}
	sdio_claim_host(g_card->func);
	*val = sdio_readl(g_card->func, offset, &ret);
	sdio_release_host(g_card->func);
	return ret;
}

static void btmtk_sdio_print_debug_sr(void)
{
	u32 ret = 0;
	u32 CCIR_Value = 0;
	u32 CHLPCR_Value = 0;
	u32 CSDIOCSR_Value = 0;
	u32 CHISR_Value = 0;
	u32 CHIER_Value = 0;
	u32 CTFSR_Value = 0;
	u32 CRPLR_Value = 0;
	unsigned char F8_Value = 0;
	unsigned char F9_Value = 0;
	unsigned char FA_Value = 0;
	unsigned char FB_Value = 0;
	unsigned char FC_Value = 0;
	unsigned char FD_Value = 0;
	unsigned char FE_Value = 0;
	unsigned char FF_Value = 0;

	ret = btmtk_sdio_readl(CCIR, &CCIR_Value);
	ret = btmtk_sdio_readl(CHLPCR, &CHLPCR_Value);
	ret = btmtk_sdio_readl(CSDIOCSR, &CSDIOCSR_Value);
	ret = btmtk_sdio_readl(CHISR, &CHISR_Value);
	ret = btmtk_sdio_readl(CHIER, &CHIER_Value);
	ret = btmtk_sdio_readl(CTFSR, &CTFSR_Value);
	ret = btmtk_sdio_readl(CRPLR, &CRPLR_Value);
	sdio_claim_host(g_card->func);
	F8_Value = sdio_f0_readb(g_card->func, 0XF8, &ret);
	F9_Value = sdio_f0_readb(g_card->func, 0XF9, &ret);
	FA_Value = sdio_f0_readb(g_card->func, 0XFA, &ret);
	FB_Value = sdio_f0_readb(g_card->func, 0XFB, &ret);
	FC_Value = sdio_f0_readb(g_card->func, 0XFC, &ret);
	FD_Value = sdio_f0_readb(g_card->func, 0XFD, &ret);
	FE_Value = sdio_f0_readb(g_card->func, 0XFE, &ret);
	FF_Value = sdio_f0_readb(g_card->func, 0XFF, &ret);
	sdio_release_host(g_card->func);
	pr_info("%s: CCIR: 0x%x, CHLPCR: 0x%x, CSDIOCSR: 0x%x, CHISR: 0x%x, CHIER: 0x%x, CTFSR: 0x%x, CRPLR: 0x%x\n",
		__func__, CCIR_Value, CHLPCR_Value, CSDIOCSR_Value, CHISR_Value, CHIER_Value, CTFSR_Value, CRPLR_Value);
	pr_info("%s: CCCR F8: 0x%x, F9: 0x%x, FA: 0x%x, FB: 0x%x, FC: 0x%x, FD: 0x%x, FE: 0x%x, FE: 0x%x\n",
		__func__, F8_Value, F9_Value, FA_Value, FB_Value, FC_Value, FD_Value, FE_Value, FF_Value);
}

struct sk_buff *btmtk_create_send_data(struct sk_buff *skb)
{
	struct sk_buff *queue_skb = NULL;
	u32 sdio_header_len = skb->len + BTM_HEADER_LEN;

	if (skb_headroom(skb) < (BTM_HEADER_LEN)) {
		queue_skb = bt_skb_alloc(sdio_header_len, GFP_ATOMIC);
		if (queue_skb == NULL) {
			pr_err("bt_skb_alloc fail return\n");
			return 0;
		}

		queue_skb->data[0] = (sdio_header_len & 0x0000ff);
		queue_skb->data[1] = (sdio_header_len & 0x00ff00) >> 8;
		queue_skb->data[2] = 0;
		queue_skb->data[3] = 0;
		queue_skb->data[4] = bt_cb(skb)->pkt_type;
		queue_skb->len = sdio_header_len;
		memcpy(&queue_skb->data[5], &skb->data[0], skb->len);
		kfree_skb(skb);
	} else {
		queue_skb = skb;
		skb_push(queue_skb, BTM_HEADER_LEN);
		queue_skb->data[0] = (sdio_header_len & 0x0000ff);
		queue_skb->data[1] = (sdio_header_len & 0x00ff00) >> 8;
		queue_skb->data[2] = 0;
		queue_skb->data[3] = 0;
		queue_skb->data[4] = bt_cb(skb)->pkt_type;
	}

	pr_info("%s end\n", __func__);
	return queue_skb;
}

static void btmtk_sdio_set_no_fw_own(struct btmtk_private *priv, bool no_fw_own)
{
	if (priv) {
		priv->no_fw_own = no_fw_own;
		pr_warn("%s set no_fw_own %d\n", __func__, priv->no_fw_own);
	} else
		pr_warn("%s priv is NULL\n", __func__);
}

static int btmtk_sdio_set_own_back(int owntype)
{
	/*Set driver own*/
	int ret = 0;
	int retry_ret = 0;
	u32 u32LoopCount = 0;
	u32 u32ReadCRValue = 0;
	u32 ownValue = 0;
	u32 set_checkretry = 30;
	int i = 0;

	pr_debug("%s owntype %d\n", __func__, owntype);

	if (user_rmmod)
		set_checkretry = 1;

	if (owntype == FW_OWN && (g_priv)) {
		if (g_priv->no_fw_own) {
			printk_ratelimited(KERN_WARNING
				"%s no_fw_own is on, just return\n", __func__);

			return ret;
		}
	}

	ret = btmtk_sdio_readl(CHLPCR, &u32ReadCRValue);

	pr_debug("%s btmtk_sdio_readl  CHLPCR done\n", __func__);
	if (owntype == DRIVER_OWN) {
		if ((u32ReadCRValue&0x100) == 0x100) {
			pr_debug("%s already driver own 0x%0x, return\n",
				__func__, u32ReadCRValue);
			goto set_own_end;
		}
	} else if (owntype == FW_OWN) {
		if ((u32ReadCRValue&0x100) == 0) {
			pr_debug("%s already FW own 0x%0x, return\n",
				__func__, u32ReadCRValue);
			goto set_own_end;
		}
	}

setretry:

	if (owntype == DRIVER_OWN)
		ownValue = 0x00000200;
	else
		ownValue = 0x00000100;

	pr_debug("%s write CHLPCR 0x%x\n", __func__, ownValue);
	ret = btmtk_sdio_writel(CHLPCR, ownValue);
	if (ret) {
		ret = -EINVAL;
		goto done;
	}
	pr_debug("%s write CHLPCR 0x%x done\n", __func__, ownValue);

	u32LoopCount = 1000;

	if (owntype == DRIVER_OWN) {
		do {
			udelay(1);
			ret = btmtk_sdio_readl(CHLPCR, &u32ReadCRValue);
			u32LoopCount--;
			pr_debug("%s DRIVER_OWN btmtk_sdio_readl CHLPCR 0x%x\n",
				__func__, u32ReadCRValue);
		} while ((u32LoopCount > 0) &&
			((u32ReadCRValue&0x100) != 0x100));

		if ((u32LoopCount == 0) && (0x100 != (u32ReadCRValue&0x100))
				&& (set_checkretry > 0)) {
			pr_warn("%s retry set_check driver own, CHLPCR 0x%x\n",
				__func__, u32ReadCRValue);
			for (i = 0; i < 3; i++) {
				ret = btmtk_sdio_readl(SWPCDBGR, &u32ReadCRValue);
				pr_warn("%s ret %d,SWPCDBGR 0x%x, and not sleep!\n",
					__func__, ret, u32ReadCRValue);
			}
			btmtk_sdio_print_debug_sr();

			set_checkretry--;
			mdelay(20);
			goto setretry;
		}
	} else {
		do {
			udelay(1);
			ret = btmtk_sdio_readl(CHLPCR, &u32ReadCRValue);
			u32LoopCount--;
			pr_debug("%s FW_OWN btmtk_sdio_readl CHLPCR 0x%x\n",
				__func__, u32ReadCRValue);
		} while ((u32LoopCount > 0) && ((u32ReadCRValue&0x100) != 0));

		if ((u32LoopCount == 0) &&
				((u32ReadCRValue&0x100) != 0) &&
				(set_checkretry > 0)) {
			pr_warn("%s retry set_check FW own, CHLPCR 0x%x\n",
				__func__, u32ReadCRValue);
			set_checkretry--;
			goto setretry;
		}
	}

	pr_debug("%s CHLPCR(0x%x), is 0x%x\n",
		__func__, CHLPCR, u32ReadCRValue);

	if (owntype == DRIVER_OWN) {
		if ((u32ReadCRValue&0x100) == 0x100)
			pr_debug("%s check %04x, is 0x100 driver own success\n",
				__func__, CHLPCR);
		else {
			pr_debug("%s check %04x, is %x shuld be 0x100\n",
				__func__, CHLPCR, u32ReadCRValue);
			ret = -EINVAL;
			goto done;
		}
	} else {
		if (0x0 == (u32ReadCRValue&0x100))
			pr_debug("%s check %04x, bit 8 is 0 FW own success\n",
				__func__, CHLPCR);
		else{
			pr_debug("%s bit 8 should be 0, %04x bit 8 is %04x\n",
				__func__, u32ReadCRValue,
				(u32ReadCRValue&0x100));
			ret = -EINVAL;
			goto done;
		}
	}

done:
	if (owntype == DRIVER_OWN) {
		if (ret) {
			pr_err("%s set driver own fail\n", __func__);
			for (i = 0; i < 8; i++) {
				retry_ret = btmtk_sdio_readl(SWPCDBGR, &u32ReadCRValue);
				pr_err("%s retry_ret %d,SWPCDBGR 0x%x, then sleep 100ms\n",
					__func__, retry_ret, u32ReadCRValue);
				msleep(100);
			}
		} else
			pr_debug("%s set driver own success\n", __func__);
	} else if (owntype == FW_OWN) {
		if (ret)
			pr_err("%s set FW own fail\n", __func__);
		else
			pr_debug("%s set FW own success\n", __func__);
	} else
		pr_err("%s unknown type %d\n", __func__, owntype);

set_own_end:

	return ret;
}

static int btmtk_sdio_enable_interrupt(int enable)
{
	u32 ret = 0;
	u32 cr_value = 0;

	if (enable)
		cr_value |= C_FW_INT_EN_SET;
	else
		cr_value |= C_FW_INT_EN_CLEAR;

	ret = btmtk_sdio_writel(CHLPCR, cr_value);
	pr_debug("%s enable %d write CHLPCR 0x%08x\n",
			__func__, enable, cr_value);

	return ret;
}

static int btmtk_sdio_get_rx_unit(struct btmtk_sdio_card *card)
{
	u8 reg;
	int ret;

	reg = sdio_readb(card->func, card->reg->card_rx_unit, &ret);
	if (!ret)
		card->rx_unit = reg;

	return ret;
}

static int btmtk_sdio_enable_host_int_mask(
				struct btmtk_sdio_card *card,
				u8 mask)
{
	int ret;

	sdio_writeb(card->func, mask, card->reg->host_int_mask, &ret);
	if (ret) {
		pr_err("Unable to enable the host interrupt!\n");
		ret = -EIO;
	}

	return ret;
}

static int btmtk_sdio_disable_host_int_mask(
				struct btmtk_sdio_card *card,
				u8 mask)
{
	u8 host_int_mask;
	int ret;

	host_int_mask = sdio_readb(card->func, card->reg->host_int_mask, &ret);
	if (ret)
		return -EIO;

	host_int_mask &= ~mask;

	sdio_writeb(card->func, host_int_mask, card->reg->host_int_mask, &ret);
	if (ret < 0) {
		pr_err("Unable to disable the host interrupt!\n");
		return -EIO;
	}

	return 0;
}

/*for debug*/
int btmtk_print_buffer_conent(u8 *buf, u32 Datalen)
{
	int i = 0;
	int print_finish = 0;
	/*Print out txbuf data for debug*/
	for (i = 0; i <= (Datalen-1); i += 16) {
		if ((i+16) <= Datalen) {
			pr_debug("%s: %02X%02X%02X%02X%02X %02X%02X%02X%02X%02X %02X%02X%02X%02X%02X %02X\n",
				__func__,
				buf[i], buf[i+1], buf[i+2], buf[i+3],
				buf[i+4], buf[i+5], buf[i+6], buf[i+7],
				buf[i+8], buf[i+9], buf[i+10], buf[i+11],
				buf[i+12], buf[i+13], buf[i+14], buf[i+15]);
		} else {
			for (; i < (Datalen); i++)
				pr_debug("%s: %02X\n", __func__, buf[i]);

			print_finish = 1;
		}

		if (print_finish)
			break;
	}
	return 0;
}

static int btmtk_sdio_send_tx_data(u8 *buffer, int tx_data_len)
{
	int ret = 0;
	u8 MultiBluckCount = 0;
	u8 redundant = 0;

	MultiBluckCount = tx_data_len/SDIO_BLOCK_SIZE;
	redundant = tx_data_len % SDIO_BLOCK_SIZE;

	if (redundant)
		tx_data_len = (MultiBluckCount+1)*SDIO_BLOCK_SIZE;

	sdio_claim_host(g_card->func);
	ret = sdio_writesb(g_card->func, CTDR, buffer, tx_data_len);
	sdio_release_host(g_card->func);

	return ret;
}

static unsigned char rxbuf_debug[MTK_RXDATA_SIZE];
static u32 rxdebug_length;

static int btmtk_sdio_recv_rx_data(void)
{
	int ret = 0;
	u32 u32ReadCRValue = 0;
	int retry_count = 500;
	u32 sdio_header_length = 0;

	memset(rxbuf, 0, MTK_RXDATA_SIZE);
	memset(rxbuf_debug, 0, MTK_RXDATA_SIZE);
	rxdebug_length = 0;

	do {
		ret = btmtk_sdio_readl(CHISR, &u32ReadCRValue);
		pr_debug("%s: loop Get CHISR 0x%08X\n",
			__func__, u32ReadCRValue);
		rx_length = (u32ReadCRValue & RX_PKT_LEN) >> 16;
		if (rx_length == 0xFFFF) {
			pr_warn("%s: 0xFFFF==rx_length, error return -EIO\n",
				__func__);
			ret = -EIO;
			break;
		}

		if ((RX_DONE&u32ReadCRValue) && rx_length) {
			pr_debug("%s: u32ReadCRValue = %08X\n",
				__func__, u32ReadCRValue);
			u32ReadCRValue &= 0xFFFB;
			ret = btmtk_sdio_writel(CHISR, u32ReadCRValue);
			pr_debug("%s: write = %08X\n",
				__func__, u32ReadCRValue);


			sdio_claim_host(g_card->func);
			ret = sdio_readsb(g_card->func, rxbuf, CRDR, rx_length);
			sdio_release_host(g_card->func);
			sdio_header_length = (rxbuf[1] << 8);
			sdio_header_length |= rxbuf[0];
			memcpy(rxbuf_debug, rxbuf, rx_length);
			rxdebug_length = rx_length;

			if (sdio_header_length != rx_length) {
				pr_err("%s sdio header length %d, rx_length %d mismatch\n",
					__func__, sdio_header_length,
					rx_length);
				if (g_priv->adapter->fops_mode != true)
					ret = -EIO;
				break;
			}

			if (sdio_header_length == 0) {
				pr_warn("%s: get sdio_header_length = %d\n",
					__func__, sdio_header_length);
				continue;
			}

			break;
		}

		retry_count--;
		if (retry_count <= 0) {
			pr_warn("%s: retry_count = %d,timeout\n",
				__func__, retry_count);
			ret = -EIO;
			break;
		}

		/* msleep(1); */
		mdelay(3);
		pr_debug("%s: retry_count = %d,wait\n", __func__, retry_count);

		if (ret)
			break;
	} while (1);

	if (ret)
		return -EIO;

	return ret;
}

static int btmtk_sdio_send_wmt_reset(void)
{
	int ret = 0;
	u8 wmt_event[8] = {4, 0xE4, 5, 2, 7, 1, 0, 0};
	u8 mtksdio_packet_header[MTK_SDIO_PACKET_HEADER_SIZE] = {0};
	u8 mtksdio_wmt_reset[9] = {1, 0x6F, 0xFC, 5, 1, 7, 1, 0, 4};

	pr_info("%s:\n", __func__);
	mtksdio_packet_header[0] = sizeof(mtksdio_packet_header) +
		sizeof(mtksdio_wmt_reset);

	memcpy(txbuf, mtksdio_packet_header, MTK_SDIO_PACKET_HEADER_SIZE);
	memcpy(txbuf+MTK_SDIO_PACKET_HEADER_SIZE, mtksdio_wmt_reset,
		sizeof(mtksdio_wmt_reset));

	btmtk_sdio_send_tx_data(txbuf,
		MTK_SDIO_PACKET_HEADER_SIZE+sizeof(mtksdio_wmt_reset));
	btmtk_sdio_recv_rx_data();

	/*compare rx data is wmt reset correct response or not*/
	if (memcmp(wmt_event, rxbuf+MTK_SDIO_PACKET_HEADER_SIZE,
			sizeof(wmt_event)) != 0) {
		ret = -EIO;
		pr_warn("%s: fail\n", __func__);
	}

	return ret;
}

static u32 btmtk_sdio_bt_memRegister_read(u32 cr)
{
	int retrytime = 3;
	u32 result = 0;
	u8 wmt_event[15] = {0x04, 0xE4, 0x10, 0x02,
				0x08, 0x0C/*0x1C*/, 0x00, 0x00,
				0x00, 0x00, 0x01, 0x00,
				0x00, 0x00, 0x80};
	/* msleep(1000); */
	u8 mtksdio_packet_header[MTK_SDIO_PACKET_HEADER_SIZE] = {0};
	u8 mtksdio_wmt_cmd[16] = {0x1, 0x6F, 0xFC, 0x0C,
				0x01, 0x08, 0x08, 0x00,
				0x02, 0x01, 0x00, 0x01,
				0x00, 0x00, 0x00, 0x00};
	mtksdio_packet_header[0] = sizeof(mtksdio_packet_header)
				+ sizeof(mtksdio_wmt_cmd);
	pr_info("%s: read cr %x\n", __func__, cr);

	memcpy(&mtksdio_wmt_cmd[12], &cr, sizeof(cr));

	memcpy(txbuf, mtksdio_packet_header, MTK_SDIO_PACKET_HEADER_SIZE);
	memcpy(txbuf + MTK_SDIO_PACKET_HEADER_SIZE, mtksdio_wmt_cmd,
		sizeof(mtksdio_wmt_cmd));

	btmtk_sdio_send_tx_data(txbuf,
		MTK_SDIO_PACKET_HEADER_SIZE + sizeof(mtksdio_wmt_cmd));
	btmtk_print_buffer_conent(txbuf,
		MTK_SDIO_PACKET_HEADER_SIZE + sizeof(mtksdio_wmt_cmd));

	do {
		usleep_range(10*1000, 15*1000);
		btmtk_sdio_recv_rx_data();
		retrytime--;
		if (retrytime <= 0)
			break;

		pr_info("%s: retrytime %d\n", __func__, retrytime);
	} while (!rxbuf[0]);

	btmtk_print_buffer_conent(rxbuf, rx_length);
	/* compare rx data is wmt reset correct response or not */
#if 0
	if (memcmp(wmt_event,
			rxbuf+MTK_SDIO_PACKET_HEADER_SIZE,
			sizeof(wmt_event)) != 0) {
		ret = -EIO;
		pr_info("%s: fail\n", __func__);
	}
#endif
	memcpy(&result, rxbuf+MTK_SDIO_PACKET_HEADER_SIZE + sizeof(wmt_event),
		sizeof(result));
	pr_info("%s: ger cr 0x%x value 0x%x\n", __func__, cr, result);
	return result;
}

/* 1:on ,  0:off */
static int btmtk_sdio_bt_set_power(u8 onoff)
{
	int ret = 0;
	int retrytime = 60;
	u8 wmt_event[8] = {4, 0xE4, 5, 2, 6, 1, 0, 0};
	u8 mtksdio_packet_header[MTK_SDIO_PACKET_HEADER_SIZE] = {0};
	u8 mtksdio_wmt_cmd[10] = {1, 0x6F, 0xFC, 6, 1, 6, 2, 0, 0, 1};

	if (onoff == 0)
		retrytime = 3;

	mtksdio_packet_header[0] =
		sizeof(mtksdio_packet_header) + sizeof(mtksdio_wmt_cmd);
	pr_info("%s: onoff %d\n", __func__, onoff);

	mtksdio_wmt_cmd[9] = onoff;

	memcpy(txbuf, mtksdio_packet_header, MTK_SDIO_PACKET_HEADER_SIZE);
	memcpy(txbuf+MTK_SDIO_PACKET_HEADER_SIZE, mtksdio_wmt_cmd,
		sizeof(mtksdio_wmt_cmd));

	btmtk_sdio_send_tx_data(txbuf,
		MTK_SDIO_PACKET_HEADER_SIZE+sizeof(mtksdio_wmt_cmd));

	do {
		msleep(100);
		btmtk_sdio_recv_rx_data();
		retrytime--;
		if (retrytime <= 0)
			break;

		if (retrytime < 40)
			pr_warn("%s: retry over 2s, retrytime %d\n",
				__func__, retrytime);

		pr_debug("%s: retrytime %d\n", __func__, retrytime);
	} while (!rxbuf[0]);


	/*compare rx data is wmt reset correct response or not*/
	if (memcmp(wmt_event, rxbuf+MTK_SDIO_PACKET_HEADER_SIZE,
			sizeof(wmt_event)) != 0) {
		ret = -EIO;
		pr_info("%s: fail\n", __func__);
	}

	return ret;
}

#if BTMTK_BIN_FILE_MODE
static int btmtk_sdio_send_and_check(u8 *cmd, u16 cmd_len,
						u8 *event, u16 event_len)
{
	int ret = 0;
	int retrytime = 60;
	int len = 0;
	u8 mtksdio_packet_header[MTK_SDIO_PACKET_HEADER_SIZE] = {0};

	len = MTK_SDIO_PACKET_HEADER_SIZE + cmd_len;

	mtksdio_packet_header[0] = (len & 0x0000ff);
	mtksdio_packet_header[1] = (len & 0x00ff00) >> 8;

	memcpy(txbuf, mtksdio_packet_header, MTK_SDIO_PACKET_HEADER_SIZE);
	memcpy(txbuf + MTK_SDIO_PACKET_HEADER_SIZE, cmd, cmd_len);

	btmtk_sdio_send_tx_data(txbuf, len);

	if (event && (event_len != 0)) {
		do {
			msleep(100);
			btmtk_sdio_recv_rx_data();
			retrytime--;
			if (retrytime <= 0)
				break;

			if (retrytime < 40)
				pr_warn("%s: retry over 2s, retrytime %d\n",
					__func__, retrytime);
		} while (!rxbuf[0]);

		if (memcmp(event, rxbuf + MTK_SDIO_PACKET_HEADER_SIZE,
				event_len) != 0) {
			ret = -EIO;
			pr_warn("%s: fail\n", __func__);
		}
	}

	return ret;
}
/*get 1 byte only*/
static int btmtk_efuse_read(u16 addr, u8 *value)
{
	uint8_t efuse_r[] = {0x01, 0x6F, 0xFC, 0x0E,
						0x01, 0x0D, 0x0A, 0x00, 0x02, 0x04,
						0x00, 0x00, 0x00, 0x00,
						0x00, 0x00, 0x00, 0x00};/*4 sub block number(sub block 0~3)*/

	uint8_t efuse_r_event[] = {0x04, 0xE4, 0x1E, 0x02, 0x0D, 0x1A, 0x00, 02, 04};
	/*check event
	 *04 E4 LEN(1B) 02 0D LEN(2Byte) 02 04 ADDR(2Byte) VALUE(4B) ADDR(2Byte) VALUE(4Byte)
	 *ADDR(2Byte) VALUE(4B)  ADDR(2Byte) VALUE(4Byte)
	 */
	int ret = 0;
	uint8_t sub_block_addr_in_event = 0;
	uint8_t sub_block = (addr / 16)*4;
	uint8_t temp = 0;

	efuse_r[10] = sub_block & 0xFF;
	efuse_r[11] = (sub_block & 0xFF00) >> 8;
	efuse_r[12] = (sub_block + 1) & 0xFF;
	efuse_r[13] = ((sub_block + 1) & 0xFF00) >> 8;
	efuse_r[14] = (sub_block + 2) & 0xFF;
	efuse_r[15] = ((sub_block + 2) & 0xFF00) >> 8;
	efuse_r[16] = (sub_block + 3) & 0xFF;
	efuse_r[17] = ((sub_block + 3) & 0xFF00) >> 8;

	ret = btmtk_sdio_send_and_check(efuse_r, sizeof(efuse_r), efuse_r_event, sizeof(efuse_r_event));
	if (ret) {
		pr_warn("%s: btmtk_sdio_send_and_check error\n", __func__);
		pr_warn("%s: rx_length %d\n", __func__, rx_length);
		return ret;
	}

	if (memcmp(rxbuf + MTK_SDIO_PACKET_HEADER_SIZE, efuse_r_event, sizeof(efuse_r_event)) == 0) {
		/*compare rxbuf format ok, compare addr*/
		pr_debug("%s: compare rxbuf format ok\n", __func__);
		if (efuse_r[10] == rxbuf[9 + MTK_SDIO_PACKET_HEADER_SIZE] &&
			efuse_r[11] == rxbuf[10 + MTK_SDIO_PACKET_HEADER_SIZE] &&
			efuse_r[12] == rxbuf[15 + MTK_SDIO_PACKET_HEADER_SIZE] &&
			efuse_r[13] == rxbuf[16 + MTK_SDIO_PACKET_HEADER_SIZE] &&
			efuse_r[14] == rxbuf[21 + MTK_SDIO_PACKET_HEADER_SIZE] &&
			efuse_r[15] == rxbuf[22 + MTK_SDIO_PACKET_HEADER_SIZE] &&
			efuse_r[16] == rxbuf[27 + MTK_SDIO_PACKET_HEADER_SIZE] &&
			efuse_r[17] == rxbuf[28 + MTK_SDIO_PACKET_HEADER_SIZE]) {

			pr_debug("%s: address compare ok\n", __func__);
			/*Get value*/
			sub_block_addr_in_event = ((addr / 16) / 4);/*cal block num*/
			temp = addr % 16;
			pr_debug("%s: address in block %d\n", __func__, temp);
			switch (temp) {
			case 0:
			case 1:
			case 2:
			case 3:
				*value = rxbuf[11 + temp + MTK_SDIO_PACKET_HEADER_SIZE];
				break;
			case 4:
			case 5:
			case 6:
			case 7:
				*value = rxbuf[17 + temp + MTK_SDIO_PACKET_HEADER_SIZE];
				break;
			case 8:
			case 9:
			case 10:
			case 11:
				*value = rxbuf[22 + temp + MTK_SDIO_PACKET_HEADER_SIZE];
				break;

			case 12:
			case 13:
			case 14:
			case 15:
				*value = rxbuf[34 + temp + MTK_SDIO_PACKET_HEADER_SIZE];
				break;
			}


		} else {
			pr_warn("%s: address compare fail\n", __func__);
			ret = -1;
		}


	} else {
		pr_warn("%s: compare rxbuf format fail\n", __func__);
		ret = -1;
	}

	return ret;
}


static bool btmtk_is_bin_file_mode(uint8_t *buf, size_t buf_size)
{
	char *p_buf = NULL;
	char *ptr = NULL, *p = NULL;
	bool ret = true;
	u16 addr = 1;
	u8 value = 0;

	if (!buf) {
		pr_warn("%s: buf is null\n", __func__);
		return false;
	} else if (buf_size < (strlen(E2P_MODE) + 2)) {
		pr_warn("%s: incorrect buf size(%d)\n",
			__func__, (int)buf_size);
		return false;
	}

	p_buf = kmalloc(buf_size + 1, GFP_KERNEL);
	if (!p_buf)
		return false;
	memcpy(p_buf, buf, buf_size);
	p_buf[buf_size] = '\0';

	/* find string */
	p = ptr = strstr(p_buf, E2P_MODE);
	if (!ptr) {
		pr_notice("%s: Can't find %s\n", __func__, E2P_MODE);
		ret = false;
		goto out;
	}

	if (p > p_buf) {
		p--;
		while ((*p == ' ') && (p != p_buf))
			p--;
		if (*p == '#') {
			pr_notice("%s: It's not EEPROM - Bin file mode\n", __func__);
			ret = false;
			goto out;
		}
	}

	/* check access mode */
	ptr += (strlen(E2P_MODE) + 1);

	if (*ptr == AUTO_MODE) {
		pr_notice("%s: It's Auto mode: %c\n", __func__, *ptr);
		ret = btmtk_efuse_read(addr, &value);
		if (ret) {
			pr_notice("%s: btmtk_efuse_read fail\n", __func__);
			pr_notice("%s: Use EEPROM Bin file mode\n", __func__);
			ret = true;
		} else if (value == 0x76) {
			pr_notice("%s: get efuse[1]: 0x%02x\n", __func__, value);
			pr_notice("%s: use efuse mode", __func__);
			ret = false;
		} else {
			pr_notice("%s: get efuse[1]: 0x%02x\n", __func__, value);
			pr_notice("%s: Use EEPROM Bin file mode\n", __func__);
			ret = true;
		}
		goto out;
	} else if (*ptr == BIN_FILE_MODE)
		pr_notice("%s: It's EEPROM bin mode: %c\n", __func__, *ptr);
	else {
		pr_warn("%s: It's not Auto/EEPROM mode %c\n", __func__, *ptr);
		ret = false;
	}

out:
	kfree(p_buf);
	return ret;
}

static void btmtk_set_eeprom2ctrler(uint8_t *buf,
						size_t buf_size,
						bool is7668)
{
	int ret = -1;
	uint8_t set_bdaddr[] = {0x01, 0x1A, 0xFC, 0x06,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	uint8_t set_bdaddr_e[] = {0x04, 0x0E, 0x04, 0x01,
			0x1A, 0xFC, 0x00};
	uint8_t set_radio[] = {0x01, 0x79, 0xFC, 0x08,
			0x07, 0x80, 0x00, 0x06, 0x07, 0x07, 0x00, 0x00};
	uint8_t set_radio_e[] = {0x04, 0x0E, 0x04, 0x01,
			0x79, 0xFC, 0x00};
	uint8_t set_pwr_offset[] = {0x01, 0x93, 0xFC, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	uint8_t set_pwr_offset_e[] = {0x04, 0x0E, 0x04, 0x01,
			0x93, 0xFC, 0x00};
	uint8_t set_xtal[] = {0x01, 0x0E, 0xFC, 0x02, 0x00, 0x00};
	uint8_t set_xtal_e[] = {0x04, 0x0E, 0x04, 0x01,
			0x0E, 0xFC, 0x00};
	uint16_t offset = 0;

	if (!buf) {
		pr_warn("%s: buf is null\n", __func__);
		return;
	} else if ((is7668 == true && buf_size < 0x389)
			|| (is7668 == false && buf_size < 0x133)) {
		pr_warn("%s: incorrect buf size(%d)\n",
			__func__, (int)buf_size);
		return;
	}

	/* set BD address */
	if (is7668)
		offset = 0x384;
	else
		offset = 0x1A;

	set_bdaddr[4] = *(buf + offset);
	set_bdaddr[5] = *(buf + offset + 1);
	set_bdaddr[6] = *(buf + offset + 2);
	set_bdaddr[7] = *(buf + offset + 3);
	set_bdaddr[8] = *(buf + offset + 4);
	set_bdaddr[9] = *(buf + offset + 5);
	if (0x0 == set_bdaddr[4] && 0x0 == set_bdaddr[5]
			&& 0x0 == set_bdaddr[6] && 0x0 == set_bdaddr[7]
			&& 0x0 == set_bdaddr[8] && 0x0 == set_bdaddr[9]) {
		pr_notice("%s: BDAddr is Zero, not set\n", __func__);
	} else {
		ret = btmtk_sdio_send_and_check(set_bdaddr, sizeof(set_bdaddr),
						set_bdaddr_e, sizeof(set_bdaddr_e));
		pr_notice("%s: set BDAddress(%02X-%02X-%02X-%02X-%02X-%02X) %s\n",
				__func__,
				set_bdaddr[9], set_bdaddr[8], set_bdaddr[7],
				set_bdaddr[6], set_bdaddr[5], set_bdaddr[4],
				ret < 0 ? "fail" : "OK");
	}

	/* radio setting - BT power */
	if (is7668) {
		offset = 0x382;
		/* BT default power */
		set_radio[4] = (*(buf + offset) & 0x07);
		/* BLE default power */
		set_radio[8] = (*(buf + offset + 1) & 0x07);
		/* TX MAX power */
		set_radio[9] = (*(buf + offset) & 0x70) >> 4;
		/* TX power sub level */
		set_radio[10] = (*(buf + offset + 1) & 0x30) >> 4;
		/* BR/EDR power diff mode */
		set_radio[11] = (*(buf + offset + 1) & 0xc0) >> 6;
	} else {
		offset = 0x132;
		/* BT default power */
		set_radio[4] = *(buf + offset);
		/* BLE default power(no this for 7662 in table) */
		set_radio[8] = *(buf + offset);
		/* TX MAX power */
		set_radio[9] = *(buf + offset + 1);
	}
	ret = btmtk_sdio_send_and_check(set_radio, sizeof(set_radio),
				set_radio_e, sizeof(set_radio_e));
	pr_notice("%s: set radio(BT/BLE default power: %d/%d MAX power: %d) %s\n",
			__func__,
			set_radio[4], set_radio[8], set_radio[9],
			ret < 0 ? "fail" : "OK");

	/*
	 * BT TX power compensation for low, middle and high
	 * channel
	 */
	if (is7668) {
		offset = 0x36D;
		/* length */
		set_pwr_offset[3] = 6;
		/* Group 0 CH 0 ~ CH14 */
		set_pwr_offset[4] = *(buf + offset);
		/* Group 1 CH15 ~ CH27 */
		set_pwr_offset[5] = *(buf + offset + 1);
		/* Group 2 CH28 ~ CH40 */
		set_pwr_offset[6] = *(buf + offset + 2);
		/* Group 3 CH41 ~ CH53 */
		set_pwr_offset[7] = *(buf + offset + 3);
		/* Group 4 CH54 ~ CH66 */
		set_pwr_offset[8] = *(buf + offset + 4);
		/* Group 5 CH67 ~ CH84 */
		set_pwr_offset[9] = *(buf + offset + 5);
	} else {
		offset = 0x139;
		/* length */
		set_pwr_offset[3] = 3;
		/* low channel */
		set_pwr_offset[4] = *(buf + offset);
		/* middle channel */
		set_pwr_offset[5] = *(buf + offset + 1);
		/* high channel */
		set_pwr_offset[6] = *(buf + offset + 2);
	}
	ret = btmtk_sdio_send_and_check(set_pwr_offset, sizeof(set_pwr_offset),
				set_pwr_offset_e, sizeof(set_pwr_offset_e));
	pr_notice("%s: set power offset(%02X %02X %02X %02X %02X %02X) %s\n",
			__func__,
			set_pwr_offset[4], set_pwr_offset[5],
			set_pwr_offset[6], set_pwr_offset[7],
			set_pwr_offset[8], set_pwr_offset[9],
			ret < 0 ? "fail" : "OK");

	/* XTAL setting */
	if (is7668) {
		offset = 0xF4;
		/* BT default power */
		set_xtal[4] = *(buf + offset);
		set_xtal[5] = *(buf + offset + 1);
		ret = btmtk_sdio_send_and_check(set_xtal, sizeof(set_xtal),
					set_xtal_e, sizeof(set_xtal_e));
		pr_notice("%s: set XTAL(0x%02X %02X) %s\n",
				__func__,
				set_xtal[4], set_xtal[5],
				ret < 0 ? "fail" : "OK");
	}
}

static void btmtk_eeprom_bin_file(struct btmtk_sdio_card *card)
{
	char *cfg_file = NULL;
	char bin_file[32];

	const struct firmware *cfg_fw = NULL;
	const struct firmware *bin_fw = NULL;

	int ret = -1;
	int chipid = card->func->device;

	pr_notice("%s: %X series\n", __func__, chipid);
	cfg_file = E2P_ACCESS_MODE_SWITCHER;
	sprintf(bin_file, E2P_BIN_FILE, chipid);

	usleep_range(10*1000, 15*1000);

	/* request configure file */
	ret = request_firmware(&cfg_fw, cfg_file, &card->func->dev);
	if (ret < 0) {
		if (ret == -ENOENT)
			pr_notice("%s: Configure file not found, ignore EEPROM bin file\n",
				__func__);
		else
			pr_notice("%s: request configure file fail(%d)\n",
				__func__, ret);
		return;
	}

	if (btmtk_is_bin_file_mode((uint8_t *)cfg_fw->data, cfg_fw->size) == false)
		goto exit2;

	usleep_range(10*1000, 15*1000);

	/* open bin file for EEPROM */
	ret = request_firmware(&bin_fw, bin_file, &card->func->dev);
	if (ret < 0) {
		pr_notice("%s: request bin file fail(%d)\n",
			__func__, ret);
		goto exit2;
	}

	/* set parameters to controller */
	btmtk_set_eeprom2ctrler((uint8_t *)bin_fw->data,
				bin_fw->size,
				(card->func->device == 0x7668 ? true : false));

exit2:
	if (cfg_fw)
		release_firmware(cfg_fw);
	if (bin_fw)
		release_firmware(bin_fw);
}
#endif
/* 1:on ,  0:off */
static int btmtk_sdio_set_sleep(void)
{
	int ret = 0;
	int i = 0;
	u8 wmt_event[] = {4, 0xE, 4, 1, 0x7A, 0xFC, 0};
	u8 mtksdio_packet_header[MTK_SDIO_PACKET_HEADER_SIZE] = {0};
	u8 mtksdio_wmt_cmd[11] = {1, 0x7A, 0xFC, 7,
		/*3:sdio, 5:usb*/03,
		/*host non sleep duration*/0x80, 0x02,
		/*host non sleep duration*/0x80, 0x02, 0x0, 0x00};

	mtksdio_packet_header[0] =
		sizeof(mtksdio_packet_header) + sizeof(mtksdio_wmt_cmd);
	pr_info("%s begin\n", __func__);

	memcpy(txbuf, mtksdio_packet_header, MTK_SDIO_PACKET_HEADER_SIZE);
	memcpy(txbuf+MTK_SDIO_PACKET_HEADER_SIZE, mtksdio_wmt_cmd,
		sizeof(mtksdio_wmt_cmd));

	btmtk_sdio_send_tx_data(txbuf,
		MTK_SDIO_PACKET_HEADER_SIZE + sizeof(mtksdio_wmt_cmd));

	btmtk_sdio_recv_rx_data();
	btmtk_print_buffer_conent(rxbuf, rx_length);
	/*compare rx data is wmt reset correct response or not*/
	for (i = 0; i < (rx_length - sizeof(wmt_event)); i++) {
		if (memcmp(wmt_event, rxbuf + MTK_SDIO_PACKET_HEADER_SIZE + i,
				sizeof(wmt_event)) != 0) {
			ret = -EIO;
			pr_err("%s: fail compare move %d\n", __func__, i);
		} else {
			pr_info("%s compare event success\n", __func__);
			ret = 0;
			break;
		}
	}

	return ret;
}

static int btmtk_sdio_host_to_card(struct btmtk_private *priv,
				u8 *payload, u16 nb)
{
	struct btmtk_sdio_card *card = priv->btmtk_dev.card;
	int ret = 0;
	int i = 0;
	u8 MultiBluckCount = 0;
	u8 redundant = 0;

	if (payload != txbuf) {
		memset(txbuf, 0, MTK_TXDATA_SIZE);
		memcpy(txbuf, payload, nb);
	}

	if (!card || !card->func) {
		pr_err("card or function is NULL!\n");
		return -EINVAL;
	}

	MultiBluckCount = nb/SDIO_BLOCK_SIZE;
	redundant = nb % SDIO_BLOCK_SIZE;

	if (redundant)
		nb = (MultiBluckCount+1)*SDIO_BLOCK_SIZE;

	if (nb < 16)
		btmtk_print_buffer_conent(txbuf, nb);
	else
		btmtk_print_buffer_conent(txbuf, 16);


	do {
		/* Transfer data to card */
		sdio_claim_host(card->func);
		ret = sdio_writesb(card->func, CTDR, txbuf, nb);
		sdio_release_host(card->func);
		if (ret < 0) {
			i++;
			pr_err("i=%d writesb failed: %d\n", i, ret);
			ret = -EIO;
			if (i > MAX_WRITE_IOMEM_RETRY)
				goto exit;
		}
	} while (ret);


	priv->btmtk_dev.tx_dnld_rdy = false;

exit:

	return ret;
}

static int btmtk_sdio_set_i2s_slave(void)
{
	int ret = 0;
	u8 wmt_event[7] = { 0x04, 0x0E, 0x04, 0x01, 0x72, 0xFC, 0x00 };
#ifdef MTK_CHIP_PCM /* For PCM setting */
	u8 cmd[] = { 0x01, 0x72, 0xFC, 0x04, 0x03, 0x10, 0x00, 0x4A };
#else /* For I2S setting */
	u8 cmd[] = { 0x01, 0x72, 0xFC, 0x04, 0x03, 0x10, 0x00, 0x02 };
#endif
	u8 mtksdio_packet_header[MTK_SDIO_PACKET_HEADER_SIZE] = { 0 };

	pr_info("%s\n", __func__);
	mtksdio_packet_header[0] = sizeof(mtksdio_packet_header) + sizeof(cmd);
	memcpy(txbuf, mtksdio_packet_header, MTK_SDIO_PACKET_HEADER_SIZE);
	memcpy(txbuf + MTK_SDIO_PACKET_HEADER_SIZE, cmd, sizeof(cmd));
	btmtk_sdio_send_tx_data(txbuf, MTK_SDIO_PACKET_HEADER_SIZE + sizeof(cmd));

	btmtk_sdio_recv_rx_data();

	pr_debug("%s rx_length %d\n", __func__, rx_length);
	pr_debug("%s rx_length %d %02x%02x%02x%02x%02x\%02x%02x%02x%02x%02x%02x%02x\n", __func__,
		   rx_length, rxbuf[0], rxbuf[1], rxbuf[2], rxbuf[3], rxbuf[4]
		   , rxbuf[5], rxbuf[6], rxbuf[7], rxbuf[8], rxbuf[9]
		   , rxbuf[10], rxbuf[11]);
	/* compare rx data is wmt reset correct response or not */
	if (memcmp(wmt_event, rxbuf + MTK_SDIO_PACKET_HEADER_SIZE, sizeof(wmt_event)) != 0) {
		ret = -EIO;
		pr_err("%s: fail\n", __func__);
	}
	return ret;
}

static int btmtk_sdio_read_pin_mux_setting(u32 *value)
{
	int ret = 0;
	u8 cmd[] = { 1, 0xD1, 0xFC, 4, 0x54, 0x30, 0x02, 0x81 };
	u8 tempvalue = 0;
	u8 mtksdio_packet_header[MTK_SDIO_PACKET_HEADER_SIZE] = { 0 };

	pr_info("%s\n", __func__);
	mtksdio_packet_header[0] = sizeof(mtksdio_packet_header) + sizeof(cmd);
	memcpy(txbuf, mtksdio_packet_header, MTK_SDIO_PACKET_HEADER_SIZE);
	memcpy(txbuf + MTK_SDIO_PACKET_HEADER_SIZE, cmd, sizeof(cmd));
	pr_debug("%s tx buf %02x%02x%02x%02x%02x\%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
		   __func__, txbuf[0], txbuf[1], txbuf[2], txbuf[3], txbuf[4]
		   , txbuf[5], txbuf[6], txbuf[7], txbuf[8], txbuf[9]
		   , txbuf[10], txbuf[11], txbuf[12], txbuf[13], txbuf[14]);
	btmtk_sdio_send_tx_data(txbuf, MTK_SDIO_PACKET_HEADER_SIZE + sizeof(cmd));

	btmtk_sdio_recv_rx_data();

	pr_debug("%s rx_length %d\n", __func__, rx_length);
	pr_debug
	    ("%s rx_length %d %02x%02x%02x%02x%02x\%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
	     __func__, rx_length, rxbuf[0], rxbuf[1], rxbuf[2], rxbuf[3], rxbuf[4]
	     , rxbuf[5], rxbuf[6], rxbuf[7], rxbuf[8], rxbuf[9]
	     , rxbuf[10], rxbuf[11], rxbuf[12], rxbuf[13], rxbuf[14]);

	if (rx_length >= 15) {
		tempvalue = rxbuf[14];
		*value = (rxbuf[14] << 24) + (rxbuf[13] << 16) + (rxbuf[12] << 8) + rxbuf[11];
		pr_debug("%s value=%08x\n", __func__, *value);
	} else {
		ret = -EIO;
		pr_err("%s get event error rx_length %d\n", __func__, rx_length);
	}

	return ret;
}

static int btmtk_sdio_write_pin_mux_setting(u32 value)
{
	int ret = 0;
	u8 event[7] = { 4, 0xE, 4, 1, 0xD0, 0xFC, 0 };
	u8 cmd[12] = { 1, 0xD0, 0xFC, 8, 0x54, 0x30, 0x02, 0x81, 0, 0, 0, 0 };
	u8 mtksdio_packet_header[MTK_SDIO_PACKET_HEADER_SIZE] = { 0 };

	pr_info("%s begin\n", __func__);
	cmd[8] = (value & 0x000000FF);
	cmd[9] = ((value & 0x0000FF00) >> 8);
	cmd[10] = ((value & 0x00FF0000) >> 16);
	cmd[11] = ((value & 0xFF000000) >> 24);

	pr_debug("%s value=%08x begin\n", __func__, value);
	mtksdio_packet_header[0] = sizeof(mtksdio_packet_header) + sizeof(cmd);
	memcpy(txbuf, mtksdio_packet_header, MTK_SDIO_PACKET_HEADER_SIZE);
	memcpy(txbuf + MTK_SDIO_PACKET_HEADER_SIZE, cmd, sizeof(cmd));

	pr_debug("%s tx buf %02x%02x%02x%02x%02x\%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
		   __func__, txbuf[0], txbuf[1], txbuf[2], txbuf[3], txbuf[4],
		   txbuf[5], txbuf[6], txbuf[7], txbuf[8], txbuf[9],
		   txbuf[10], txbuf[11], txbuf[12], txbuf[13], txbuf[14], txbuf[15]);

	btmtk_sdio_send_tx_data(txbuf, MTK_SDIO_PACKET_HEADER_SIZE + sizeof(cmd));

	btmtk_sdio_recv_rx_data();

	pr_debug("%s rx_length %d\n", __func__, rx_length);
	pr_debug
	    ("%s rx_length %d %02x%02x%02x%02x%02x\%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
	     __func__, rx_length, rxbuf[0], rxbuf[1], rxbuf[2], rxbuf[3], rxbuf[4]
	     , rxbuf[5], rxbuf[6], rxbuf[7], rxbuf[8], rxbuf[9]
	     , rxbuf[10], rxbuf[11], rxbuf[12], rxbuf[13], rxbuf[14]);

	/* compare rx data is wmt reset correct response or not */
	if (memcmp(event, rxbuf + MTK_SDIO_PACKET_HEADER_SIZE, sizeof(event)) != 0) {
		ret = -EIO;
		pr_err("%s: fail\n", __func__);
	}
	return ret;

}

static int btmtk_send_rom_patch(u8 *fwbuf, u32 fwlen, int mode)
{
	int ret = 0;
	u8 mtksdio_packet_header[MTK_SDIO_PACKET_HEADER_SIZE] = {0};
	int stp_len = 0;
	u8 mtkdata_header[MTKDATA_HEADER_SIZE] = {0};

	int copy_len = 0;
	int Datalen = fwlen;
	u32 u32ReadCRValue = 0;


	pr_debug("%s fwlen %d, mode = %d\n", __func__, fwlen, mode);
	if (fwlen < Datalen) {
		pr_err("%s file size = %d,is not corect\n", __func__, fwlen);
		return -ENOENT;
	}

	stp_len = Datalen + MTKDATA_HEADER_SIZE;


	mtkdata_header[0] = 0x2;/*ACL data*/
	mtkdata_header[1] = 0x6F;
	mtkdata_header[2] = 0xFC;

	mtkdata_header[3] = ((Datalen+4+1)&0x00FF);
	mtkdata_header[4] = ((Datalen+4+1)&0xFF00)>>8;

	mtkdata_header[5] = 0x1;
	mtkdata_header[6] = 0x1;

	mtkdata_header[7] = ((Datalen+1)&0x00FF);
	mtkdata_header[8] = ((Datalen+1)&0xFF00)>>8;

	mtkdata_header[9] = mode;

/* 0 and 1 is packet length, include MTKSTP_HEADER_SIZE */
	mtksdio_packet_header[0] =
		(Datalen+4+MTKSTP_HEADER_SIZE+6)&0xFF;
	mtksdio_packet_header[1] =
		((Datalen+4+MTKSTP_HEADER_SIZE+6)&0xFF00)>>8;
	mtksdio_packet_header[2] = 0;
	mtksdio_packet_header[3] = 0;

/*
 * mtksdio_packet_header[2] and mtksdio_packet_header[3]
 * are reserved
 */
	pr_debug("%s result %02x  %02x\n", __func__,
		((Datalen+4+MTKSTP_HEADER_SIZE+6)&0xFF00)>>8,
		(Datalen+4+MTKSTP_HEADER_SIZE+6));

	memcpy(txbuf+copy_len, mtksdio_packet_header,
		MTK_SDIO_PACKET_HEADER_SIZE);
	copy_len += MTK_SDIO_PACKET_HEADER_SIZE;

	memcpy(txbuf+copy_len, mtkdata_header, MTKDATA_HEADER_SIZE);
	copy_len += MTKDATA_HEADER_SIZE;

	memcpy(txbuf+copy_len, fwbuf, Datalen);
	copy_len += Datalen;

	pr_debug("%s txbuf %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
		__func__,
		txbuf[0], txbuf[1], txbuf[2], txbuf[3], txbuf[4],
		txbuf[5], txbuf[6], txbuf[7], txbuf[8], txbuf[9]);


	ret = btmtk_sdio_readl(CHIER, &u32ReadCRValue);
	pr_debug("%s: CHIER u32ReadCRValue %x, ret %d\n",
		__func__, u32ReadCRValue, ret);

	ret = btmtk_sdio_readl(CHLPCR, &u32ReadCRValue);
	pr_debug("%s: CHLPCR u32ReadCRValue %x, ret %d\n",
		__func__, u32ReadCRValue, ret);

	ret = btmtk_sdio_readl(CHISR, &u32ReadCRValue);
	pr_debug("%s: 0CHISR u32ReadCRValue %x, ret %d\n",
		__func__, u32ReadCRValue, ret);
	ret = btmtk_sdio_readl(CHISR, &u32ReadCRValue);
	pr_debug("%s: 00CHISR u32ReadCRValue %x, ret %d\n",
		__func__, u32ReadCRValue, ret);

	btmtk_sdio_send_tx_data(txbuf, copy_len);

	ret = btmtk_sdio_recv_rx_data();

	return ret;
}



/*
 * type: cmd:1, ACL:2
 * -------------------------------------------------
 * mtksdio hedaer 4 byte| wmt header  |
 *
 *
 * data len should less than 512-4-4
 */
static int btmtk_sdio_send_wohci(u8 type, u32 len, u8 *data)
{
	u32 ret = 0;
	u32 push_in_data_len = 0;
	u32 MultiBluckCount = 0;
	u32 redundant = 0;
	u8 mtk_wmt_header[MTKWMT_HEADER_SIZE] = {0};
	u8 mtksdio_packet_header[MTK_SDIO_PACKET_HEADER_SIZE] = {0};
	u8 mtk_tx_data[512] = {0};

	mtk_wmt_header[0] = type;
	mtk_wmt_header[1] = 0x6F;
	mtk_wmt_header[2] = 0xFC;
	mtk_wmt_header[3] = len;

	mtksdio_packet_header[0] =
		(len+MTKWMT_HEADER_SIZE+MTK_SDIO_PACKET_HEADER_SIZE)&0xFF;
	mtksdio_packet_header[1] =
		((len+MTKWMT_HEADER_SIZE+MTK_SDIO_PACKET_HEADER_SIZE)&0xFF00)
		>>8;
	mtksdio_packet_header[2] = 0;
	mtksdio_packet_header[3] = 0;
/*
 * mtksdio_packet_header[2] and mtksdio_packet_header[3]
 * are reserved
 */

	memcpy(mtk_tx_data, mtksdio_packet_header,
		sizeof(mtksdio_packet_header));
	push_in_data_len += sizeof(mtksdio_packet_header);

	memcpy(mtk_tx_data+push_in_data_len, mtk_wmt_header,
		sizeof(mtk_wmt_header));
	push_in_data_len += sizeof(mtk_wmt_header);

	memcpy(mtk_tx_data+push_in_data_len, data, len);
	push_in_data_len += len;
	memcpy(txbuf, mtk_tx_data, push_in_data_len);

	MultiBluckCount = push_in_data_len/4;
	redundant = push_in_data_len % 4;
	if (redundant)
		push_in_data_len = (MultiBluckCount+1)*4;

	sdio_claim_host(g_card->func);
	ret = sdio_writesb(g_card->func, CTDR, txbuf, push_in_data_len);
	sdio_release_host(g_card->func);

	pr_info("%s retrun  0x%0x\n", __func__, ret);
	return ret;
}

/*
 * data event:
 * return
 * 0:
 * patch download is not complete/get patch semaphore fail
 * 1:
 * patch download is complete by others
 * 2
 * patch download is not coplete
 * 3:(for debug)
 * release patch semaphore success
 */
static int btmtk_sdio_need_load_rom_patch(void)
{
	u32 ret = -1;
	u8 cmd[] = {0x1, 0x17, 0x1, 0x0, 0x1};
	u8 event[] = {0x2, 0x17, 0x1, 0x0};

	do {
		ret = btmtk_sdio_send_wohci(HCI_COMMAND_PKT, sizeof(cmd), cmd);

		if (ret) {
			pr_err("%s btmtk_sdio_send_wohci return fail ret %d\n",
					__func__, ret);
			break;
		}

		ret = btmtk_sdio_recv_rx_data();
		if (ret)
			break;

		if (rx_length == 12) {
			if (memcmp(rxbuf+7, event, sizeof(event)) == 0)
				return rxbuf[11];

			pr_err("%s receive event content is not correct, print receive data\n",
				__func__);
			btmtk_print_buffer_conent(rxbuf, rx_length);
		}
	} while (0);
	pr_err("%s return ret %d\n", __func__, ret);
	return ret;
}
static int btmtk_sdio_set_write_clear(void)
{
	u32 u32ReadCRValue = 0;
	u32 ret = 0;

	ret = btmtk_sdio_readl(CHCR, &u32ReadCRValue);
	if (ret) {
		pr_err("%s read CHCR error\n", __func__);
		ret = EINVAL;
		return ret;
	}

	u32ReadCRValue |= 0x00000002;
	btmtk_sdio_writel(CHCR, u32ReadCRValue);
	pr_info("%s write CHCR 0x%08X\n", __func__, u32ReadCRValue);
	ret = btmtk_sdio_readl(CHCR, &u32ReadCRValue);
	pr_info("%s read CHCR 0x%08X\n", __func__, u32ReadCRValue);
	if (u32ReadCRValue&0x00000002)
		pr_info("%s write clear\n", __func__);
	else
		pr_info("%s read clear\n", __func__);

	return ret;
}

static int btmtk_sdio_set_i2s(void)
{
	int ret = 0;
	u32 pinmux = 0;

	ret = btmtk_sdio_set_i2s_slave();
	if (ret) {
		pr_err("btmtk_sdio_set_i2s_slave error(%d)\n", ret);
		return ret;
	}
	ret = btmtk_sdio_read_pin_mux_setting(&pinmux);
	if (ret) {
		pr_err("btmtk_sdio_read_pin_mux_setting error(%d)\n", ret);
		return ret;
	}

	pinmux &= 0x0000FFFF;
	pinmux |= 0x22220000;
	ret = btmtk_sdio_write_pin_mux_setting(pinmux);

	if (ret) {
		pr_err("btmtk_sdio_write_pin_mux_setting error(%d)\n", ret);
		return ret;
	}

	pinmux = 0;
	ret = btmtk_sdio_read_pin_mux_setting(&pinmux);
	if (ret) {
		pr_err("btmtk_sdio_read_pin_mux_setting error(%d)\n", ret);
		return ret;
	}
	pr_info("confirm pinmux %04x\n", pinmux);

	return ret;
}

static int btmtk_sdio_download_rom_patch(struct btmtk_sdio_card *card)
{
	const struct firmware *fw_firmware = NULL;
	const u8 *firmware = NULL;
	int firmwarelen, ret = 0;
	u8 *fwbuf;
	struct _PATCH_HEADER *patchHdr;
	u8 *cDateTime = NULL;
	u16 u2HwVer = 0;
	u16 u2SwVer = 0;
	u32 u4PatchVer = 0;
	u32 uhwversion = 0;

	u32 u32ReadCRValue = 0;

	int RedundantSize = 0;
	u32 bufferOffset = 0;
	u8  patch_status = 0;

	ret = btmtk_sdio_set_own_back(DRIVER_OWN);
	if (ret)
		return ret;

	patch_status = btmtk_sdio_need_load_rom_patch();

	pr_info("%s patch_status %d\n", __func__, patch_status);
	if (patch_status > PATCH_NEED_DOWNLOAD) {
		pr_err("%s patch_status error\n", __func__);
		return -1;
	}

	uhwversion = btmtk_sdio_bt_memRegister_read(HW_VERSION);
	pr_info("%s uhwversion 0x%x\n", __func__, uhwversion);

	if (uhwversion == 0x8A00) {
		pr_info("%s request_firmware(firmware name %s)\n",
				__func__, card->firmware);
		ret = request_firmware(&fw_firmware, card->firmware,
						&card->func->dev);

		if ((ret < 0) || !fw_firmware) {
			pr_err("request_firmware(firmware name %s) failed, error code = %d\n",
					card->firmware,
					ret);
			ret = -ENOENT;
			goto done;
		}
	} else {
		pr_info("%s request_firmware(firmware name %s)\n",
				__func__, card->firmware1);
		ret = request_firmware(&fw_firmware,
				card->firmware1,
				&card->func->dev);

		if ((ret < 0) || !fw_firmware) {
			pr_err("request_firmware(firmware name %s) failed, error code = %d\n",
				card->firmware1, ret);
			ret = -ENOENT;
			goto done;
		}
	}
	memset(fw_version_str, 0, FW_VERSION_BUF_SIZE);
	if ((fw_firmware->data[8] >= '0') && (fw_firmware->data[8] <= '9'))
		memcpy(fw_version_str, fw_firmware->data, FW_VERSION_SIZE - 1);
	else
		sprintf(fw_version_str, "%.4s-%.2s-%.2s.%.1s.%.2s.%.1s.%.1s.%.2s",
			fw_firmware->data, fw_firmware->data + 4, fw_firmware->data + 6,
			fw_firmware->data + 8, fw_firmware->data + 9,
			fw_firmware->data + 11, fw_firmware->data + 12,
			fw_firmware->data + 13);

	if (patch_status == PATCH_IS_DOWNLOAD_BT_OTHER ||
			patch_status == PATCH_READY) {
		pr_info("%s patch is ready no need load patch again\n",
					__func__);

		ret = btmtk_sdio_readl(0, &u32ReadCRValue);
		pr_info("%s read chipid =  %x\n", __func__, u32ReadCRValue);

		/*Set interrupt output*/
		ret = btmtk_sdio_writel(CHIER, FIRMWARE_INT|TX_FIFO_OVERFLOW |
				FW_INT_IND_INDICATOR | TX_COMPLETE_COUNT |
				TX_UNDER_THOLD | TX_EMPTY | RX_DONE);

		if (ret) {
			pr_err("Set interrupt output fail(%d)\n", ret);
			ret = -EIO;
		}

		/*enable interrupt output*/
		ret = btmtk_sdio_writel(CHLPCR, C_FW_INT_EN_SET);
		if (ret) {
			pr_err("enable interrupt output fail(%d)\n", ret);
			ret = -EIO;
			goto done;
		}

		ret = btmtk_sdio_bt_set_power(1);
		if (ret)
			goto done;

#if BTMTK_BIN_FILE_MODE
		/* Send hci cmd before sleep */
		btmtk_eeprom_bin_file(card);
#endif

		ret = btmtk_sdio_set_sleep();
		if (ret)
			goto done;

		btmtk_sdio_set_write_clear();
		ret = btmtk_sdio_set_i2s();
		goto done;
	}

	firmware = fw_firmware->data;
	firmwarelen = fw_firmware->size;

	pr_debug("Downloading FW image (%d bytes)\n", firmwarelen);

	fwbuf = (u8 *)firmware;

	/*Display rom patch info*/
	patchHdr =  (struct _PATCH_HEADER *)fwbuf;
	cDateTime = patchHdr->ucDateTime;
	u2HwVer = patchHdr->u2HwVer;
	u2SwVer = patchHdr->u2SwVer;
	u4PatchVer = patchHdr->u4PatchVer;

	pr_info("[btmtk] =============== Patch Info ==============\n");
	pr_info("[btmtk] Built Time = %s\n", cDateTime);
	pr_info("[btmtk] Hw Ver = 0x%x\n",
		((u2HwVer & 0x00ff) << 8) | ((u2HwVer & 0xff00) >> 8));
	pr_info("[btmtk] Sw Ver = 0x%x\n",
		((u2SwVer & 0x00ff) << 8) | ((u2SwVer & 0xff00) >> 8));
	pr_info("[btmtk] Patch Ver = 0x%04x\n",
			((u4PatchVer & 0xff000000) >> 24) |
			((u4PatchVer & 0x00ff0000) >> 16));

	pr_info("[btmtk] Platform = %c%c%c%c\n",
			patchHdr->ucPlatform[0],
			patchHdr->ucPlatform[1],
			patchHdr->ucPlatform[2],
			patchHdr->ucPlatform[3]);
	pr_info("[btmtk] Patch start addr = %02x\n", patchHdr->u2PatchStartAddr);
	pr_info("[btmtk] =========================================\n");

	fwbuf += sizeof(struct _PATCH_HEADER);
	pr_debug("%s PATCH_HEADER size %zd\n",
		__func__, sizeof(struct _PATCH_HEADER));
	firmwarelen -= sizeof(struct _PATCH_HEADER);

	ret = btmtk_sdio_readl(0, &u32ReadCRValue);
	pr_info("%s read chipid =  %x\n", __func__, u32ReadCRValue);

	/*Set interrupt output*/
	ret = btmtk_sdio_writel(CHIER, FIRMWARE_INT|TX_FIFO_OVERFLOW |
		FW_INT_IND_INDICATOR | TX_COMPLETE_COUNT |
		TX_UNDER_THOLD | TX_EMPTY | RX_DONE);

	if (ret) {
		pr_err("Set interrupt output fail(%d)\n", ret);
		ret = -EIO;
		goto done;
	}

	/*enable interrupt output*/
	ret = btmtk_sdio_writel(CHLPCR, C_FW_INT_EN_SET);

	if (ret) {
		pr_err("enable interrupt output fail(%d)\n", ret);
		ret = -EIO;
		goto done;
	}

	RedundantSize = firmwarelen;
	pr_debug("%s firmwarelen %d\n", __func__, firmwarelen);

	do {
		bufferOffset = firmwarelen - RedundantSize;

		if (RedundantSize == firmwarelen &&
				 RedundantSize >= PATCH_DOWNLOAD_SIZE)
			ret = btmtk_send_rom_patch(fwbuf+bufferOffset,
				PATCH_DOWNLOAD_SIZE, SDIO_PATCH_DOWNLOAD_FIRST);
		else if (RedundantSize == firmwarelen)
			ret = btmtk_send_rom_patch(fwbuf+bufferOffset,
				RedundantSize, SDIO_PATCH_DOWNLOAD_FIRST);
		else if (RedundantSize < PATCH_DOWNLOAD_SIZE) {
			ret = btmtk_send_rom_patch(fwbuf+bufferOffset,
				RedundantSize, SDIO_PATCH_DOWNLOAD_END);
			pr_debug("%s patch downoad last patch part\n",
					__func__);
		} else
			ret = btmtk_send_rom_patch(fwbuf+bufferOffset,
				PATCH_DOWNLOAD_SIZE, SDIO_PATCH_DOWNLOAD_CON);

		RedundantSize -= PATCH_DOWNLOAD_SIZE;

		if (ret) {
			pr_err("%s btmtk_send_rom_patch fail\n", __func__);
			goto done;
		}
		pr_debug("%s RedundantSize %d\n", __func__, RedundantSize);
		if (RedundantSize <= 0) {
			pr_debug("%s patch downoad finish\n", __func__);
			break;
		}
	} while (1);

	btmtk_sdio_set_write_clear();

	if (btmtk_sdio_need_load_rom_patch() == PATCH_READY)
		pr_info("%s patchdownload is done by BT\n", __func__);
	else
		pr_warn("%s patchdownload download by BT, not ready\n", __func__);


	ret = btmtk_sdio_send_wmt_reset();

	if (ret)
		goto done;

	ret = btmtk_sdio_bt_set_power(1);

	if (ret) {
		ret = -EINVAL;
		goto done;
	}

#if BTMTK_BIN_FILE_MODE
	/* Send hci cmd before sleep */
	btmtk_eeprom_bin_file(card);
#endif

	ret = btmtk_sdio_set_sleep();
	if (ret) {
		ret = -EINVAL;
		goto done;
	}

	ret = btmtk_sdio_set_i2s();

done:

	release_firmware(fw_firmware);

	if (!ret)
		pr_info("%s success\n", __func__);
	else
		pr_info("%s fail\n", __func__);

	return ret;
}

static void btmtk_sdio_close_coredump_file(void)
{
	pr_debug("%s  vfs_fsync\n", __func__);

#if SAVE_FW_DUMP_IN_KERNEL
	if (fw_dump_file)
		vfs_fsync(fw_dump_file, 0);
#endif
	fw_is_doing_coredump = false;
	fw_is_coredump_end_packet = true;

	if (fw_dump_file) {
		pr_info("%s : close file  %s\n",
			__func__, fw_dump_file_name);
#if SAVE_FW_DUMP_IN_KERNEL
		filp_close(fw_dump_file, NULL);
/* #endif */
		fw_dump_file = NULL;
#endif

	} else {
		printk_ratelimited(KERN_WARNING
			"%s : fw_dump_file is NULL can't close file %s\n",
			__func__, fw_dump_file_name);
	}
}

static void btmtk_sdio_stop_wait_dump_complete_thread(void)
{
	if (IS_ERR(wait_dump_complete_tsk) || wait_dump_complete_tsk == NULL) {
		printk_ratelimited(KERN_WARNING "%s wait_dump_complete_tsk is error", __func__);
	} else {
		kthread_stop(wait_dump_complete_tsk);
		wait_dump_complete_tsk = NULL;
	}
}

static int btmtk_skb_enq(void *src, u32 len, struct sk_buff_head *queue,
		struct _OSAL_UNSLEEPABLE_LOCK_ *spin_lock)
{
	struct sk_buff *skb = NULL;

	if (src == NULL || len == 0) {
		pr_err("%s: Incorrect parameters, src:%p, len:%d\n",
				__func__, src, len);
		return -EINVAL;
	}

	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb == NULL) {
		/* pr_err("%s: alloc_skb failure\n", __func__); */
		return -ENOMEM;
	}

	memcpy(skb->data, src, len);
	skb->len = len;

	LOCK_UNSLEEPABLE_LOCK(spin_lock);
	skb_queue_tail(queue, skb);
	UNLOCK_UNSLEEPABLE_LOCK(spin_lock);
	return 0;
}

static int btmtk_sdio_card_to_host(struct btmtk_private *priv, const u8 *event, const int event_len,
	int add_spec_header)
{
    /**
     * event: check event which want to compare
     * return value: -x fail, 0 success
     */
	u16 buf_len = 0;
	int ret = 0;
	struct sk_buff *skb = NULL;
	struct sk_buff *fops_skb = NULL;
	struct sk_buff *fwlog_fops_skb = NULL;
	u32 type;
	u32 fourbalignment_len = 0;
	u32 dump_len = 0;
	char *core_dump_end = NULL;
	int i = 0;
	int fw_dump_pkt = 0;
	u8 reset_event[] =  {0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00};
	static u8 picus_blocking_warn;
	static u8 fwdump_blocking_warn;
	char *dump_file_name;
	u16 print_count;
	u16 print_left;
	u16 j = 0;
	u32 u32ReadCRValue = 0;

	if (rx_length > (MTK_SDIO_PACKET_HEADER_SIZE + 1)) {
		buf_len = rx_length - (MTK_SDIO_PACKET_HEADER_SIZE + 1);
	} else {
		pr_info("%s, rx_length error(%d)\n", __func__, rx_length);
		return -EINVAL;
	}

#if SUPPORT_FW_DUMP
	fw_is_coredump_end_packet = false;
	if (rx_length > (SDIO_HEADER_LEN + 8) && rxbuf[SDIO_HEADER_LEN] == 0x80 &&
		rxbuf[SDIO_HEADER_LEN + 5] == 0x6F && rxbuf[SDIO_HEADER_LEN + 6] == 0xFC) {
		dump_len = (rxbuf[SDIO_HEADER_LEN + 1] & 0x0F) * 256
				+ rxbuf[SDIO_HEADER_LEN + 2];
		pr_debug("%s: get dump length %d\n", __func__, dump_len);

		if (print_dump_data_counter < PRINT_DUMP_COUNT) {
			print_dump_data_counter++;
			pr_warn("%s : dump %d %s\n",
				__func__, print_dump_data_counter, &rxbuf[SDIO_HEADER_LEN + 9]);
			if (print_dump_data_counter == 1 && (strstr(&rxbuf[SDIO_HEADER_LEN + 9], "assert") == NULL)
				&& (strstr(&rxbuf[SDIO_HEADER_LEN + 9], "ASSERT") == NULL)) {
				print_dump_data_counter = 0;
				/*driver may receive coredump data after end*/
				pr_warn("%s : rx_length %d %02x%02x%02x%02x %02x%02x%02x%02x%02x %02x%02x%02x%02x%02x\n",
					__func__, rx_length,
					rxbuf[0], rxbuf[1], rxbuf[2], rxbuf[3],
					rxbuf[4], rxbuf[5], rxbuf[6], rxbuf[7], rxbuf[8],
					rxbuf[9], rxbuf[10], rxbuf[11], rxbuf[12], rxbuf[13]);
				goto SKIP_DUMP;
			}
#ifdef MTK_KERNEL_DEBUG
		/* release mode do reset dongle if print dump finish */
		} else {
			if (print_dump_data_counter == PRINT_DUMP_COUNT) {
			/* create dump file fail and is user mode */
				pr_info("%s: user mode, do reset after print dump done %d, disable interrupt\n",
					__func__, print_dump_data_counter);
				print_dump_data_counter++;
				picus_blocking_warn = 0;
				fwdump_blocking_warn = 0;
				btmtk_sdio_close_coredump_file();
				btmtk_sdio_stop_wait_dump_complete_thread();
			}
			fw_is_doing_coredump = true;
			goto SKIP_DUMP;
#endif
		}
		fw_is_doing_coredump = true;

#if SAVE_FW_DUMP_IN_KERNEL
		if (fw_dump_file == NULL && print_dump_data_counter == 1) {
			/* #if SAVE_FW_DUMP_IN_KERNEL */
			pr_info("%s: create btmtk_sdio_wait_dump_complete_thread\n", __func__);
			wait_dump_complete_tsk = kthread_run(btmtk_sdio_wait_dump_complete_thread,
				NULL, "btmtk_sdio_wait_dump_complete_thread");
			msleep(50);
			if (!wait_dump_complete_tsk)
				pr_err("%s: wait_dump_complete_tsk is NULL\n", __func__);

			btmtk_sdio_notify_wlan_remove_start();
			btmtk_sdio_set_no_fw_own(g_priv, TRUE);

			for (i = 0; i < ARRAY_SIZE(COREDUMP_FILE_NAME); i++) {
				memset(fw_dump_file_name, 0, sizeof(fw_dump_file_name));
				dump_file_name = COREDUMP_FILE_NAME[i];
				snprintf(fw_dump_file_name, sizeof(fw_dump_file_name),
					"%s_%d", dump_file_name, current_fwdump_file_number);
				pr_warn("%s : open file %s\n", __func__, fw_dump_file_name);
				fw_dump_file = filp_open(fw_dump_file_name, O_RDWR | O_CREAT, 0644);

				if (!(IS_ERR(fw_dump_file))) {
					pr_warn("%s : open file %s success\n", __func__,
						fw_dump_file_name);
				} else {
					pr_warn("%s : open file %s fail\n",	__func__,
						fw_dump_file_name);
					fw_dump_file = NULL;
					continue;
				}

				if (fw_dump_file && fw_dump_file->f_op == NULL) {
					pr_warn("%s : %s fw_dump_file->f_op is NULL, close\n",
						fw_dump_file_name, __func__);
					filp_close(fw_dump_file, NULL);
					fw_dump_file = NULL;
					continue;
				}

				if (fw_dump_file && fw_dump_file->f_op->write == NULL) {
					pr_warn("%s : %s fw_dump_file->f_op->write is NULL, close\n",
						fw_dump_file_name, __func__);
					filp_close(fw_dump_file, NULL);
					fw_dump_file = NULL;
					continue;
				}
				/* open file success */
				current_fwdump_file_number++;
				break;
			}

		}

		if ((dump_len > 0) && fw_dump_file && fw_dump_file->f_op
				&& fw_dump_file->f_op->write)
			fw_dump_file->f_op->write(fw_dump_file, &rxbuf[SDIO_HEADER_LEN + 9],
				dump_len, &fw_dump_file->f_pos);
#endif /* SAVE_FW_DUMP_IN_KERNEL */

		if (skb_queue_len(&g_priv->adapter->fwlog_fops_queue) < FWLOG_ASSERT_QUEUE_COUNT) {
			/* This is coredump data, save coredump data to picus_queue */
			pr_debug("%s : Receive coredump data, move data to fwlog queue for picus",
				__func__);
			buf_len = rx_length - (MTK_SDIO_PACKET_HEADER_SIZE + 1);
			btmtk_skb_enq(&rxbuf[MTK_SDIO_PACKET_HEADER_SIZE + 5], buf_len,
					&g_priv->adapter->fwlog_fops_queue, &fwlog_metabuffer.spin_lock);
			wake_up_interruptible(&fw_log_inq);
			fwdump_blocking_warn = 0;
		} else if (fwdump_blocking_warn == 0) {
			fwdump_blocking_warn = 1;
			pr_warn("btmtk FW dump queue size is full");
		}

		if (dump_len >= strlen(FW_DUMP_END_EVENT)) {
			core_dump_end = strstr(&rxbuf[SDIO_HEADER_LEN + 10],
					FW_DUMP_END_EVENT);

			if (core_dump_end) {
				pr_warn("%s : core_dump_end %s, rxbuf = %02x %02x %02x\n",
					__func__, core_dump_end, rxbuf[4], rxbuf[5], rxbuf[6]);
				pr_warn("%s : get core_dump_end %s\n", __func__, core_dump_end);
				sdio_claim_host(g_card->func);
				sdio_release_irq(g_card->func);
				sdio_release_host(g_card->func);
				print_dump_data_counter = 0;
				picus_blocking_warn = 0;
				fwdump_blocking_warn = 0;
				btmtk_sdio_close_coredump_file();

				btmtk_sdio_stop_wait_dump_complete_thread();
			}
		}

		/* Modify header to ACL format, handle is 0xFFF0
		 * Core dump header:
		 * 80 AA BB CC DD 6F FC XX XX XX ......
		 * 80    ->    02 (ACL TYPE)
		 * AA BB ->    FF F0
		 * CC DD ->    Data length
		 */
		rxbuf[SDIO_HEADER_LEN] = HCI_ACLDATA_PKT;
		rxbuf[SDIO_HEADER_LEN + 3] = (buf_len - 4) % 256;
		rxbuf[SDIO_HEADER_LEN + 4] = (buf_len - 4) / 256;
		rxbuf[SDIO_HEADER_LEN + 1] = 0xFF;
		rxbuf[SDIO_HEADER_LEN + 2] = 0xF0;
		fw_dump_pkt = 1;
	}
#endif /* SUPPORT_FW_DUMP */

SKIP_DUMP:

	if (rx_length > (SDIO_HEADER_LEN + 8) && rxbuf[SDIO_HEADER_LEN] == 0x80 &&
		rxbuf[SDIO_HEADER_LEN + 5] == 0x6F && rxbuf[SDIO_HEADER_LEN + 6] == 0xFC) {
		rxbuf[SDIO_HEADER_LEN] = HCI_ACLDATA_PKT;
		rxbuf[SDIO_HEADER_LEN + 3] = (buf_len - 4) % 256;
		rxbuf[SDIO_HEADER_LEN + 4] = (buf_len - 4) / 256;
		rxbuf[SDIO_HEADER_LEN + 1] = 0xFF;
		rxbuf[SDIO_HEADER_LEN + 2] = 0xF0;
		fw_dump_pkt = 1;
	} else if (rx_length > (SDIO_HEADER_LEN + 4)) {
		pr_debug("%s This is debug log data, length = %d", __func__,
				(rxbuf[SDIO_HEADER_LEN + 1] & 0x0F) * 256 + rxbuf[SDIO_HEADER_LEN + 2]);
		if (rxbuf[SDIO_HEADER_LEN + 1] == 0xFF && rxbuf[SDIO_HEADER_LEN + 3] == 0x50) {
			/* receive picus data to fwlog_queue */
			if (skb_queue_len(&g_priv->adapter->fwlog_fops_queue) < FWLOG_QUEUE_COUNT) {
				/* picus header is 0x50 */
				pr_debug("%s : This is picus data", __func__);
				buf_len = rx_length - (MTK_SDIO_PACKET_HEADER_SIZE + 1);
				LOCK_UNSLEEPABLE_LOCK(&(fwlog_metabuffer.spin_lock));
				fwlog_fops_skb = bt_skb_alloc(buf_len, GFP_ATOMIC);
				memcpy(&fwlog_fops_skb->data[0], &rxbuf[MTK_SDIO_PACKET_HEADER_SIZE + 1], buf_len);
				fwlog_fops_skb->len = buf_len;
				skb_queue_tail(&g_priv->adapter->fwlog_fops_queue, fwlog_fops_skb);
				pr_debug("%s fwlog_fops_skb length = %d, buf_len = %d\n",
					__func__, fwlog_fops_skb->len, buf_len);
				if (skb_queue_empty(&g_priv->adapter->fwlog_fops_queue))
					pr_warn("%s fwlog_fops_queue is empty empty\n", __func__);
				wake_up_interruptible(&fw_log_inq);
				UNLOCK_UNSLEEPABLE_LOCK(&(fwlog_metabuffer.spin_lock));
				picus_blocking_warn = 0;
			} else {
				if (picus_blocking_warn == 0) {
					picus_blocking_warn = 1;
					pr_warn("btmtk_usb Picus queue size is full");
				}
			}
			goto exit;
		}
	}

	type = rxbuf[MTK_SDIO_PACKET_HEADER_SIZE];

	btmtk_print_buffer_conent(rxbuf, rx_length);

	/* Read the length of data to be transferred , not include pkt type*/
	buf_len = rx_length-(MTK_SDIO_PACKET_HEADER_SIZE + 1);

	pr_debug("%s buf_len : %d\n", __func__, buf_len);
	if (rx_length <= SDIO_HEADER_LEN) {
		pr_warn("invalid packet length: %d\n", buf_len);
		ret = -EINVAL;
		goto exit;
	}

	/* Allocate buffer */
	/* rx_length = num_blocks * blksz + BTSDIO_DMA_ALIGN*/
	skb = bt_skb_alloc(rx_length, GFP_ATOMIC);
	if (skb == NULL) {
		pr_warn("No free skb\n");
		ret = -ENOMEM;
		goto exit;
	}

	pr_debug("%s rx_length %d,buf_len %d\n", __func__, rx_length, buf_len);

	memcpy(skb->data, &rxbuf[MTK_SDIO_PACKET_HEADER_SIZE + 1], buf_len);

	switch (type) {
	case HCI_ACLDATA_PKT:
		pr_debug("%s data[2] 0x%02x, data[3] 0x%02x\n",
				__func__, skb->data[2], skb->data[3]);
		buf_len = skb->data[2] + skb->data[3] * 256 + 4;
		pr_debug("%s acl buf_len %d\n", __func__, buf_len);
		break;
	case HCI_SCODATA_PKT:
		buf_len = skb->data[3] + 3;
		break;
	case HCI_EVENT_PKT:
		buf_len = skb->data[1] + 2;
		break;
	default:
		BTSDIO_INFO_RAW(skb->data, (buf_len < 16 ? buf_len : 16), "%s1:  skb->data(type %d):", __func__, type);
		if (rxdebug_length > 256)
			rxdebug_length = 256;
		print_count = rxdebug_length / 16;
		print_left = rxdebug_length % 16;
		for (j = 0; j < print_count; j++)
			BTSDIO_INFO_RAW(rxbuf_debug+(j*16), 16, "%s2:  sdio raw data:", __func__);
		if (print_left > 0)
			BTSDIO_INFO_RAW(rxbuf_debug+(j*16), print_left, "%s3:  sdio raw data:", __func__);
		for (j = 0; j < 5; j++) {
			ret = btmtk_sdio_readl(SWPCDBGR, &u32ReadCRValue);
			pr_info("%s ret %d,SWPCDBGR 0x%x, and not sleep!\n",
				__func__, ret, u32ReadCRValue);
		}
		btmtk_sdio_print_debug_sr();

		/* trigger fw core dump */
		if (g_priv->adapter->fops_mode == true)
			btmtk_sdio_trigger_fw_assert();
		ret = -EINVAL;
		goto exit;
	}

	if ((g_priv->adapter->fops_mode == true) &&
		memcmp(skb->data, reset_event, sizeof(reset_event)) == 0) {
		pr_info("%s get reset complete event!\n", __func__);
		need_set_i2s = 1;
	}
	if (event_compare_status == BTMTK_SDIO_EVENT_COMPARE_STATE_NEED_COMPARE)
		BTSDIO_DEBUG_RAW(skb->data, buf_len, "%s: skb->data :", __func__);

	if ((buf_len >= sizeof(READ_ADDRESS_EVENT))
		&& (event_compare_status == BTMTK_SDIO_EVENT_COMPARE_STATE_NEED_COMPARE)) {
		if ((memcmp(skb->data, READ_ADDRESS_EVENT, sizeof(READ_ADDRESS_EVENT)) == 0) && (buf_len == 12)) {
			for (i = 0; i < BD_ADDRESS_SIZE; i++)
				g_card->bdaddr[i] = skb->data[6 + i];

			pr_debug("%s: GET TV BDADDR = %02X:%02X:%02X:%02X:%02X:%02X", __func__,
			g_card->bdaddr[0], g_card->bdaddr[1], g_card->bdaddr[2],
			g_card->bdaddr[3], g_card->bdaddr[4], g_card->bdaddr[5]);

			/*
			 * event_compare_status =
			 * BTMTK_SDIO_EVENT_COMPARE_STATE_COMPARE_SUCCESS;
			 */
		} else
			pr_debug("%s READ_ADDRESS_EVENT compare fail buf_len %d\n", __func__, buf_len);
	}


	if (((!fw_is_doing_coredump) &&
			(!fw_is_coredump_end_packet)) || fw_dump_pkt) {
		if (event_compare_status == BTMTK_SDIO_EVENT_COMPARE_STATE_NEED_COMPARE) {
			/*compare with tx hci cmd*/
			pr_debug("%s buf_len %d, event_need_compare_len %d\n",
				__func__, buf_len, event_need_compare_len);
			if (buf_len >= event_need_compare_len) {
				if (memcmp(skb->data, event_need_compare, event_need_compare_len) == 0) {
					event_compare_status = BTMTK_SDIO_EVENT_COMPARE_STATE_COMPARE_SUCCESS;
					pr_info("%s compare success and goto exit !\n", __func__);
					/*
					 * for VTS loopback mode test, the command send by driver
					 * event should not report to stack
					 */
					kfree_skb(skb);
					goto exit;
				} else {
					pr_debug("%s compare fail\n", __func__);
					BTSDIO_DEBUG_RAW(event_need_compare, event_need_compare_len,
						"%s: event_need_compare :", __func__);
				}
			}
		}

		if (type == 0x80) {
			if (buf_len > 7) {
				pr_info("%s drop the packet, packet type: %02x, len: %d, Rawdata: %02x%02x %02x%02x%02x%02x\n",
					__func__, type, buf_len, skb->data[0], skb->data[1], skb->data[2],
					skb->data[3], skb->data[4], skb->data[5]);
			} else {
				pr_info("%s drop the packet, packet type: %02x, len: %d\n", __func__, type, buf_len);
			}
			kfree_skb(skb);
			goto exit;
		}

		fops_skb = bt_skb_alloc(buf_len, GFP_ATOMIC);
		bt_cb(fops_skb)->pkt_type = type;
		memcpy(fops_skb->data, skb->data, buf_len);

		fops_skb->len = buf_len;
		LOCK_UNSLEEPABLE_LOCK(&(metabuffer.spin_lock));
		skb_queue_tail(&g_priv->adapter->fops_queue, fops_skb);
		if (skb_queue_empty(&g_priv->adapter->fops_queue))
			pr_info("%s fops_queue is empty\n", __func__);
		UNLOCK_UNSLEEPABLE_LOCK(&(metabuffer.spin_lock));

		kfree_skb(skb);

		wake_up_interruptible(&inq);
		goto exit;
	} else
		kfree_skb(skb);


exit:
	if (ret) {
		pr_info("%s fail free skb\n", __func__);
		kfree_skb(skb);
		return ret;
	}

	buf_len += 1;
	if (buf_len % 4)
		fourbalignment_len = buf_len + 4 - (buf_len % 4);
	else
		fourbalignment_len = buf_len;

	rx_length -= fourbalignment_len;

	if (rx_length > (MTK_SDIO_PACKET_HEADER_SIZE)) {
		memcpy(&rxbuf[MTK_SDIO_PACKET_HEADER_SIZE],
		&rxbuf[MTK_SDIO_PACKET_HEADER_SIZE+fourbalignment_len],
		rx_length-MTK_SDIO_PACKET_HEADER_SIZE);
	}

	pr_debug("%s ret %d, rx_length, %d,fourbalignment_len %d <--\n",
		__func__, ret, rx_length, fourbalignment_len);
	return ret;
}

static int btmtk_sdio_process_int_status(
		struct btmtk_private *priv)
{
	int ret = 0;
	u32 u32rxdatacount = 0;
	u32 u32ReadCRValue = 0;

	ret = btmtk_sdio_readl(CHISR, &u32ReadCRValue);
	pr_debug("%s CHISR 0x%08x\n", __func__, u32ReadCRValue);
	if (u32ReadCRValue & FIRMWARE_INT_BIT15) {
		btmtk_sdio_set_no_fw_own(g_priv, TRUE);
		btmtk_sdio_writel(CHISR, FIRMWARE_INT_BIT15);
	}

	pr_debug("%s check TX_EMPTY CHISR 0x%08x\n", __func__, u32ReadCRValue);
	if (TX_EMPTY&u32ReadCRValue) {
		ret = btmtk_sdio_writel(CHISR, (TX_EMPTY | TX_COMPLETE_COUNT));
		priv->btmtk_dev.tx_dnld_rdy = true;
		pr_debug("%s set tx_dnld_rdy 1\n", __func__);
	}

	if (RX_DONE&u32ReadCRValue)
		ret = btmtk_sdio_recv_rx_data();

	if (ret == 0) {
		while (rx_length > (MTK_SDIO_PACKET_HEADER_SIZE)) {
			ret = btmtk_sdio_card_to_host(priv, NULL, -1, 0);
			if (ret)
				break;
			u32rxdatacount++;
			pr_debug("%s u32rxdatacount %d, rx_length %d\n",
				__func__, u32rxdatacount, rx_length);
		}
	}


	ret = btmtk_sdio_enable_interrupt(1);

	return ret;
}

static void btmtk_sdio_interrupt(struct sdio_func *func)
{
	struct btmtk_private *priv;
	struct btmtk_sdio_card *card;

	card = sdio_get_drvdata(func);

	if (!card)
		return;


	if (!card->priv)
		return;

	priv = card->priv;
	btmtk_sdio_enable_interrupt(0);

	btmtk_interrupt(priv);
}

static int btmtk_sdio_register_dev(struct btmtk_sdio_card *card)
{
	struct sdio_func *func;
	u32	u32ReadCRValue = 0;
	u8 reg;
	int ret = 0;

	if (!card || !card->func) {
		pr_err("Error: card or function is NULL!\n");
		ret = -EINVAL;
		goto failed;
	}

	func = card->func;

	sdio_claim_host(func);

	ret = sdio_enable_func(func);
	if (ret) {
		pr_err("sdio_enable_func() failed: ret=%d\n", ret);
		ret = -EIO;
		goto release_host;
	}

	btmtk_sdio_readb(SDIO_CCCR_IENx, &u32ReadCRValue);
	pr_info("before claim irq read SDIO_CCCR_IENx %x, func num %d\n",
		u32ReadCRValue, func->num);

	ret = sdio_claim_irq(func, btmtk_sdio_interrupt);
	if (ret) {
		pr_err("sdio_claim_irq failed: ret=%d\n", ret);
		ret = -EIO;
		goto disable_func;
	}
	pr_info("sdio_claim_irq success: ret=%d\n", ret);

	btmtk_sdio_readb(SDIO_CCCR_IENx, &u32ReadCRValue);
	pr_info("after claim irq read SDIO_CCCR_IENx %x\n", u32ReadCRValue);

	ret = sdio_set_block_size(card->func, SDIO_BLOCK_SIZE);
	if (ret) {
		pr_err("cannot set SDIO block size\n");
		ret = -EIO;
		goto release_irq;
	}

	reg = sdio_readb(func, card->reg->io_port_0, &ret);
	if (ret < 0) {
		ret = -EIO;
		goto release_irq;
	}

	card->ioport = reg;

	reg = sdio_readb(func, card->reg->io_port_1, &ret);
	if (ret < 0) {
		ret = -EIO;
		goto release_irq;
	}

	card->ioport |= (reg << 8);

	reg = sdio_readb(func, card->reg->io_port_2, &ret);
	if (ret < 0) {
		ret = -EIO;
		goto release_irq;
	}

	card->ioport |= (reg << 16);

	pr_info("SDIO FUNC%d IO port: 0x%x\n", func->num, card->ioport);

	if (card->reg->int_read_to_clear) {
		reg = sdio_readb(func, card->reg->host_int_rsr, &ret);
		if (ret < 0) {
			ret = -EIO;
			goto release_irq;
		}
		sdio_writeb(func, reg | 0x3f, card->reg->host_int_rsr, &ret);
		if (ret < 0) {
			ret = -EIO;
			goto release_irq;
		}

		reg = sdio_readb(func, card->reg->card_misc_cfg, &ret);
		if (ret < 0) {
			ret = -EIO;
			goto release_irq;
		}
		sdio_writeb(func, reg | 0x10, card->reg->card_misc_cfg, &ret);
		if (ret < 0) {
			ret = -EIO;
			goto release_irq;
		}
	}

	sdio_set_drvdata(func, card);

	sdio_release_host(func);

	return 0;

release_irq:
	sdio_release_irq(func);

disable_func:
	sdio_disable_func(func);

release_host:
	sdio_release_host(func);

failed:
	pr_info("%s fail\n", __func__);
	return ret;
}

static int btmtk_sdio_unregister_dev(struct btmtk_sdio_card *card)
{
	if (card && card->func) {
		sdio_claim_host(card->func);
		sdio_release_irq(card->func);
		sdio_disable_func(card->func);
		sdio_release_host(card->func);
		sdio_set_drvdata(card->func, NULL);
	}

	return 0;
}

static int btmtk_sdio_enable_host_int(struct btmtk_sdio_card *card)
{
	int ret;
	u32 read_data = 0;

	if (!card || !card->func)
		return -EINVAL;

	sdio_claim_host(card->func);

	ret = btmtk_sdio_enable_host_int_mask(card, HIM_ENABLE);

	btmtk_sdio_get_rx_unit(card);

	if (0) {
		typedef int (*fp_sdio_hook)(struct mmc_host *host,
						unsigned int width);
		fp_sdio_hook func_sdio_hook =
			(fp_sdio_hook)kallsyms_lookup_name("mmc_set_bus_width");
		unsigned char data = 0;

		data = sdio_f0_readb(card->func, SDIO_CCCR_IF, &ret);
		if (ret)
			pr_info("%s sdio_f0_readb ret %d\n", __func__, ret);

		pr_info("%s sdio_f0_readb data 0x%X!\n", __func__, data);

		data  &= ~SDIO_BUS_WIDTH_MASK;
		data  |= SDIO_BUS_ASYNC_INT;
		card->func->card->quirks |= MMC_QUIRK_LENIENT_FN0;

		sdio_f0_writeb(card->func, data, SDIO_CCCR_IF, &ret);
		if (ret)
			pr_info("%s sdio_f0_writeb ret %d\n", __func__, ret);

		pr_info("%s func_sdio_hook at 0x%p!\n",
			__func__, func_sdio_hook);
		if (func_sdio_hook)
			func_sdio_hook(card->func->card->host, MMC_BUS_WIDTH_1);

		data = sdio_f0_readb(card->func, SDIO_CCCR_IF, &ret);
		if (ret)
			pr_info("%s sdio_f0_readb 2 ret %d\n",
				__func__, ret);

		pr_info("%s sdio_f0_readb2 data 0x%X\n", __func__, data);
	}

	sdio_release_host(card->func);

/* workaround for some platform no host clock sometimes */

	btmtk_sdio_readl(CSDIOCSR, &read_data);
	pr_info("%s read CSDIOCSR is 0x%X\n", __func__, read_data);
	read_data |= 0x4;
	btmtk_sdio_writel(CSDIOCSR, read_data);
	pr_info("%s write CSDIOCSR is 0x%X\n", __func__, read_data);

	return ret;
}

static int btmtk_sdio_disable_host_int(struct btmtk_sdio_card *card)
{
	int ret;

	if (!card || !card->func)
		return -EINVAL;

	sdio_claim_host(card->func);

	ret = btmtk_sdio_disable_host_int_mask(card, HIM_DISABLE);

	sdio_release_host(card->func);

	return ret;
}

static int btmtk_sdio_download_fw(struct btmtk_sdio_card *card)
{
	int ret = 0;

	pr_info("%s begin\n", __func__);
	if (!card || !card->func) {
		pr_err("card or function is NULL!\n");
		return -EINVAL;
	}

	sdio_claim_host(card->func);

	if (btmtk_sdio_download_rom_patch(card)) {
		pr_err("Failed to download firmware!\n");
		ret = -EIO;
	}
	sdio_release_host(card->func);

	/**
	 * winner or not, with this test the FW synchronizes
	 * when the module can continue its initialization.
	 */
	return ret;
}

static int btmtk_sdio_push_data_to_metabuffer(
						struct ring_buffer *metabuffer,
						char *data,
						int len,
						u8 type,
						bool use_type)
{
	int remainLen = 0;

	if (metabuffer->write_p >= metabuffer->read_p)
		remainLen = metabuffer->write_p - metabuffer->read_p;
	else
		remainLen = META_BUFFER_SIZE -
			(metabuffer->read_p - metabuffer->write_p);

	if ((remainLen + 1 + len) >= META_BUFFER_SIZE) {
		pr_warn("%s copy copyLen %d > META_BUFFER_SIZE(%d), push back to queue\n",
			__func__,
			(remainLen + 1 + len),
			META_BUFFER_SIZE);
		return -1;
	}

	if (use_type) {
		metabuffer->buffer[metabuffer->write_p] = type;
		metabuffer->write_p++;
	}
	if (metabuffer->write_p >= META_BUFFER_SIZE)
		metabuffer->write_p = 0;

	if (metabuffer->write_p + len <= META_BUFFER_SIZE)
		memcpy(&metabuffer->buffer[metabuffer->write_p],
			data,
			len);
	else {
		memcpy(&metabuffer->buffer[metabuffer->write_p],
			data,
			META_BUFFER_SIZE - metabuffer->write_p);
		memcpy(metabuffer->buffer,
			&data[META_BUFFER_SIZE - metabuffer->write_p],
			len - (META_BUFFER_SIZE - metabuffer->write_p));
	}

	metabuffer->write_p += len;
	if (metabuffer->write_p >= META_BUFFER_SIZE)
		metabuffer->write_p -= META_BUFFER_SIZE;

	remainLen += (1 + len);
	return 0;
}

static int btmtk_sdio_pull_data_from_metabuffer(
						struct ring_buffer *metabuffer,
						char __user *buf,
						size_t count)
{
	int copyLen = 0;
	unsigned long ret = 0;

	if (metabuffer->write_p >= metabuffer->read_p)
		copyLen = metabuffer->write_p - metabuffer->read_p;
	else
		copyLen = META_BUFFER_SIZE -
			(metabuffer->read_p - metabuffer->write_p);

	if (copyLen > count)
		copyLen = count;

	if (metabuffer->read_p + copyLen <= META_BUFFER_SIZE)
		ret = copy_to_user(buf,
				&metabuffer->buffer[metabuffer->read_p],
				copyLen);
	else {
		ret = copy_to_user(buf,
				&metabuffer->buffer[metabuffer->read_p],
				META_BUFFER_SIZE - metabuffer->read_p);
		if (!ret)
			ret = copy_to_user(
				&buf[META_BUFFER_SIZE - metabuffer->read_p],
				metabuffer->buffer,
				copyLen - (META_BUFFER_SIZE-metabuffer->read_p));
	}

	if (ret)
		pr_warn("%s copy to user fail, ret %d\n", __func__, (int)ret);

	metabuffer->read_p += (copyLen - ret);
	if (metabuffer->read_p >= META_BUFFER_SIZE)
		metabuffer->read_p -= META_BUFFER_SIZE;

	return (copyLen - ret);
}

static int btmtk_sdio_reset_dev(struct btmtk_sdio_card *card)
{
	struct sdio_func *func;
	/* u32 u32ReadCRValue = 0; */
	u8 reg;
	int ret = 0;

	if (!card || !card->func)
		pr_err("Error: card or function is NULL!\n");

	func = card->func;

	sdio_claim_host(func);

	ret = sdio_enable_func(func);
	if (ret)
		pr_err("sdio_enable_func() failed: ret=%d\n", ret);

	reg = sdio_f0_readb(func, SDIO_CCCR_IENx, &ret);
	if (ret)
		pr_err("f0 read SDIO_CCCR_IENx %x, func num %d, ret %d\n",
			reg, func->num, ret);

	ret = sdio_claim_irq(func, btmtk_sdio_interrupt);
	pr_info("%s: sdio_claim_irq return %d\n", __func__, ret);

	reg |= 1 << func->num;
	reg |= 1;

	/* for bt driver can write SDIO_CCCR_IENx */
	func->card->quirks |= MMC_QUIRK_LENIENT_FN0;
	sdio_f0_writeb(func, reg, SDIO_CCCR_IENx, &ret);
	pr_info("f0 write ret %d SDIO_CCCR_IENx %x, func num %d\n",
		ret, reg, func->num);

	reg = sdio_f0_readb(func, SDIO_CCCR_IENx, &ret);
	pr_info("f0 read SDIO_CCCR_IENx %x, func num %d, ret %d\n",
		reg, func->num, ret);

	ret = sdio_set_block_size(card->func, SDIO_BLOCK_SIZE);
	if (ret)
		pr_err("cannot set SDIO block size\n");
	pr_info("after set block size\n");

	reg = sdio_readb(func, card->reg->io_port_0, &ret);
	if (ret < 0)
		pr_err("read io port0 fail\n");

	card->ioport = reg;

	reg = sdio_readb(func, card->reg->io_port_1, &ret);
	if (ret < 0)
		pr_err("read io port1 fail\n");

	card->ioport |= (reg << 8);

	reg = sdio_readb(func, card->reg->io_port_2, &ret);
	if (ret < 0)
		pr_err("read io port2 fail\n");

	card->ioport |= (reg << 16);

	pr_info("SDIO FUNC%d IO port: 0x%x\n", func->num, card->ioport);

	if (card->reg->int_read_to_clear) {
		reg = sdio_readb(func, card->reg->host_int_rsr, &ret);
		if (ret < 0)
			pr_err("read init rsr fail\n");
		sdio_writeb(func, reg | 0x3f, card->reg->host_int_rsr, &ret);
		if (ret < 0)
			pr_err("write init rsr fail\n");

		reg = sdio_readb(func, card->reg->card_misc_cfg, &ret);
		if (ret < 0)
			pr_err("read misc cfg fail\n");
		sdio_writeb(func, reg | 0x10, card->reg->card_misc_cfg, &ret);
		if (ret < 0)
			pr_err("write misc cfg fail\n");
	}

	sdio_set_drvdata(func, card);

	sdio_release_host(func);

	return 0;
}

static int btmtk_sdio_reset_fw(struct btmtk_sdio_card *card)
{
	int ret = 0;

	pr_info("%s Mediatek Bluetooth driver Version=%s\n",
			__func__, VERSION);

#if SUPPORT_EINT
	btmtk_sdio_RegisterBTIrq(card);
	btmtk_sdio_woble_input_init(card);
#endif
	pr_debug("%s func device %X\n", __func__, card->func->device);
	pr_debug("%s Call btmtk_sdio_register_dev\n", __func__);
	btmtk_sdio_reset_dev(card);

	pr_debug("%s btmtk_sdio_register_dev success\n", __func__);
	btmtk_sdio_enable_host_int(card);
	if (btmtk_sdio_download_fw(card)) {
		pr_err("%s Downloading firmware failed!\n", __func__);
		ret = -ENODEV;
	}

	return ret;
}

static int btmtk_sdio_set_card_clkpd(int on)
{
	int ret = -1;
	/* call sdio_set_card_clkpd in sdio host driver */
	typedef void (*psdio_set_card_clkpd) (int on);
	char *sdio_set_card_clkpd_func_name = "sdio_set_card_clkpd";
	psdio_set_card_clkpd psdio_set_card_clkpd_func =
		(psdio_set_card_clkpd)kallsyms_lookup_name(sdio_set_card_clkpd_func_name);

	if (psdio_set_card_clkpd_func) {
		pr_info("%s: get  %s\n", __func__,
				sdio_set_card_clkpd_func_name);
		psdio_set_card_clkpd_func(on);
		ret = 0;
	} else
		pr_err("%s: do not get %s\n", __func__, sdio_set_card_clkpd_func_name);
	return ret;
}


/*toggle PMU enable*/
static int btmtk_sdio_toggle_rst_pin(void)
{
	uint32_t pmu_en_delay = MT76x8_PMU_EN_DEFAULT_DELAY;
	int pmu_en;
	struct device *prDev;

	if (g_card == NULL) {
		pr_err("g_card is NULL return\n");
		return -1;
	}
	sdio_claim_host(g_card->func);
	btmtk_sdio_set_card_clkpd(0);
	sdio_release_host(g_card->func);
	prDev = mmc_dev(g_card->func->card->host);
	if (!prDev) {
		pr_err("unable to get struct dev for BT\n");
		return -1;
	}
	pmu_en = of_get_named_gpio(prDev->of_node, MT76x8_PMU_EN_PIN_NAME, 0);
	pr_info("%s pmu_en %d\n", __func__, pmu_en);
	if (gpio_is_valid(pmu_en)) {
		gpio_direction_output(pmu_en, 0);
		mdelay(pmu_en_delay);
		gpio_direction_output(pmu_en, 1);
		pr_info("%s: %s pull low/high done\n", __func__,
				MT76x8_PMU_EN_PIN_NAME);
	} else {
		pr_err("%s: *** Invalid GPIO %s ***\n", __func__,
				MT76x8_PMU_EN_PIN_NAME);
		return -1;
	}
	return 0;
}

int btmtk_sdio_notify_wlan_remove_end(void)
{
	pr_info("%s begin\n", __func__);
	wlan_remove_done = 1;
	btmtk_sdio_stop_wait_wlan_remove_tsk();

	pr_info("%s done\n", __func__);
	return 0;
}
EXPORT_SYMBOL(btmtk_sdio_notify_wlan_remove_end);

int btmtk_sdio_bt_trigger_core_dump(int trigger_dump)
{
	struct sk_buff *skb = NULL;
	u8 coredump_cmd[] = {0x6F, 0xFC, 0x05, 0x00, 0x01, 0x02, 0x01, 0x00, 0x08};

	if (g_priv == NULL) {
		pr_err("%s g_priv is NULL return\n", __func__);
		return 0;
	}

	if (wait_dump_complete_tsk) {
		pr_warn("%s wait_dump_complete_tsk is working, return\n", __func__);
		return 0;
	}

	if (wait_wlan_remove_tsk) {
		pr_warn("%s wait_wlan_remove_tsk is working, return\n", __func__);
		return 0;
	}

	if (g_priv->btmtk_dev.reset_dongle) {
		pr_warn("%s reset_dongle is true, return\n", __func__);
		return 0;
	}

	if (!probe_ready) {
		pr_info("%s probe_ready %d, return -1\n", __func__, probe_ready);
		return -1;/*BT driver is not ready, ask wifi do coredump*/
	}

	pr_info("%s: trigger_dump %d\n", __func__, trigger_dump);
	if (trigger_dump) {
		skb = bt_skb_alloc(sizeof(coredump_cmd), GFP_ATOMIC);
		bt_cb(skb)->pkt_type = HCI_COMMAND_PKT;
		memcpy(&skb->data[0], &coredump_cmd[0], sizeof(coredump_cmd));
		skb->len = sizeof(coredump_cmd);
		skb_queue_tail(&g_priv->adapter->tx_queue, skb);
		wake_up_interruptible(&g_priv->main_thread.wait_q);
	} else {
		wait_wlan_remove_tsk = kthread_run(btmtk_sdio_wait_wlan_remove_thread,
			NULL, "btmtk_sdio_wait_dump_complete_thread");
		msleep(50);
		btmtk_sdio_notify_wlan_remove_start();
	}

	return 0;
}
EXPORT_SYMBOL(btmtk_sdio_bt_trigger_core_dump);

void btmtk_sdio_notify_wlan_toggle_rst_end(void)
{
	typedef void (*pnotify_wlan_toggle_rst_end) (int reserved);
	char *notify_wlan_toggle_rst_end_func_name = "notify_wlan_toggle_rst_end";
	/*void notify_wlan_toggle_rst_end(void)*/
	pnotify_wlan_toggle_rst_end pnotify_wlan_toggle_rst_end_func =
		(pnotify_wlan_toggle_rst_end) kallsyms_lookup_name(notify_wlan_toggle_rst_end_func_name);

	if (pnotify_wlan_toggle_rst_end_func) {
		pr_info("%s: do notify %s\n", __func__, notify_wlan_toggle_rst_end_func_name);
		pnotify_wlan_toggle_rst_end_func(1);
	} else
		pr_err("%s: do not get %s\n", __func__, notify_wlan_toggle_rst_end_func_name);
}

int btmtk_sdio_reset_dongle(void)
{
	int ret = 0;
	int retry = 3;

	pr_info("%s: begin\n", __func__);
	if (g_priv == NULL) {
		pr_info("%s: g_priv = NULL, return\n", __func__);
		return -1;
	}

	need_reset_stack = 1;
	wlan_remove_done = 0;

retry_reset:
	ret = 0;
	if (btmtk_sdio_toggle_rst_pin()) {
		ret = -1;
		goto rst_dongle_err;
	}

	btmtk_sdio_set_no_fw_own(g_priv, FALSE);
	msleep(100);
	sdio_claim_host(g_card->func);
	sdio_reset_comm(g_card->func->card);
	sdio_release_host(g_card->func);
	pr_info("%s: sdio_reset_comm done\n", __func__);
	msleep(100);
	ret = btmtk_sdio_reset_fw(g_card);
	if (ret)
		pr_info("%s reset fw fail\n", __func__);
	else
		pr_info("%s reset fw done\n", __func__);

rst_dongle_err:
	if (ret && (retry > 0)) {
		pr_err("%s ret = %d, retry %d\n", __func__, ret, retry);
		retry--;
		goto retry_reset;
	}

	btmtk_sdio_notify_wlan_toggle_rst_end();

	if (g_priv) {
		g_priv->btmtk_dev.tx_dnld_rdy = 1;
		g_priv->btmtk_dev.reset_dongle = 0;
	} else
		pr_err("%s g_priv is NULL no set tx_dnld_rdy = 1\n", __func__);

	wlan_status = WLAN_STATUS_DEFAULT;
	btmtk_clean_queue();
	g_priv->btmtk_dev.reset_progress = 0;
	print_dump_data_counter = 0;
	fw_is_doing_coredump = false;
	pr_info("%s return ret = %d\n", __func__, ret);
	return ret;
}

#if SUPPORT_EINT
static irqreturn_t btmtk_sdio_woble_isr(int irq, void *dev)
{
	struct btmtk_sdio_card *data = (struct btmtk_sdio_card *)dev;
	unsigned long wait_powerkey_time = 0;

	pr_info("%s begin\n", __func__);
	disable_irq_nosync(data->wobt_irq);
	atomic_dec(&(data->irq_enable_count));
	pr_info("%s:disable BT IRQ\n", __func__);
	pr_info("%s:call wake lock\n", __func__);
	wait_powerkey_time = msecs_to_jiffies(WAIT_POWERKEY_TIMEOUT);
	wake_lock_timeout(&data->eint_wlock, wait_powerkey_time);/*10s*/
	pr_info("%s end\n", __func__);
	return IRQ_HANDLED;
}

static int btmtk_sdio_RegisterBTIrq(struct btmtk_sdio_card *data)
{
	struct device_node *eint_node = NULL;
	int interrupts[2];

	eint_node = of_find_compatible_node(NULL, NULL, "mediatek,mt7668_bt_ctrl");
	pr_info("%s begin\n", __func__);
	if (eint_node) {
		pr_info("%s Get mt76xx_bt_ctrl compatible node\n", __func__);
		data->wobt_irq = irq_of_parse_and_map(eint_node, 0);
		pr_info("%s wobt_irq number:%d", __func__, data->wobt_irq);
		if (data->wobt_irq) {
			of_property_read_u32_array(eint_node, "interrupts",
						   interrupts, ARRAY_SIZE(interrupts));
			data->wobt_irqlevel = interrupts[1];
			if (request_irq(data->wobt_irq, btmtk_sdio_woble_isr,
					data->wobt_irqlevel, "mt7668_bt_ctrl-eint", data))
				pr_info("%s WOBTIRQ LINE NOT AVAILABLE!!\n", __func__);
			else {
				pr_info("%s disable BT IRQ\n", __func__);
				disable_irq_nosync(data->wobt_irq);
			}

		} else
			pr_info("%s can't find mt76xx_bt_ctrl irq\n", __func__);

	} else {
		data->wobt_irq = 0;
		pr_info("%s can't find mt76xx_bt_ctrl compatible node\n", __func__);
	}


	pr_info("btmtk:%s: end\n", __func__);
	return 0;
}
#endif
static int btmtk_sdio_probe(struct sdio_func *func,
					const struct sdio_device_id *id)
{
	int ret = 0;
	struct btmtk_private *priv = NULL;
	struct btmtk_sdio_card *card = NULL;
	struct btmtk_sdio_device *data = (void *) id->driver_data;
	u32 u32ReadCRValue = 0;
	u8 fw_download_fail = 0;

	probe_counter++;
	pr_info("%s Mediatek Bluetooth driver Version=%s\n",
			__func__, VERSION);
	pr_info("vendor=0x%x, device=0x%x, class=%d, fn=%d, support func_num %d\n",
			id->vendor, id->device, id->class,
			func->num, data->reg->func_num);

	if (func->num != data->reg->func_num) {
		pr_info("func num is not match\n");
		return -ENODEV;
	}

	card = devm_kzalloc(&func->dev, sizeof(*card), GFP_KERNEL);
	if (!card)
		return -ENOMEM;

	card->func = func;
	g_card = card;

#if SUPPORT_EINT
	btmtk_sdio_RegisterBTIrq(card);
#endif

	if (id->driver_data) {
		card->helper = data->helper;
		card->firmware = data->firmware;
		card->firmware1 = data->firmware1;
		card->reg = data->reg;
		card->sd_blksz_fw_dl = data->sd_blksz_fw_dl;
		card->support_pscan_win_report = data->support_pscan_win_report;
		card->supports_fw_dump = data->supports_fw_dump;
		card->chip_id = data->reg->chip_id;
		card->suspend_count = 0;
		pr_info("%s chip_id is %x\n", __func__, data->reg->chip_id);
		/*allocate memory for woble_setting_file*/
		g_card->woble_setting_file_name = kzalloc(MAX_BIN_FILE_NAME_LEN, GFP_KERNEL);
		if (!g_card->woble_setting_file_name)
			return -1;

		memcpy(g_card->woble_setting_file_name,
				WOBLE_SETTING_FILE_NAME,
				sizeof(WOBLE_SETTING_FILE_NAME));
	}

	pr_debug("%s func device %X\n", __func__, card->func->device);
	pr_debug("%s Call btmtk_sdio_register_dev\n", __func__);
	if (btmtk_sdio_register_dev(card) < 0) {
		pr_err("Failed to register BT device!\n");
		return -ENODEV;
	}

	pr_debug("%s btmtk_sdio_register_dev success\n", __func__);

	/* Disable the interrupts on the card */
	btmtk_sdio_enable_host_int(card);
	pr_debug("call btmtk_sdio_enable_host_int done\n");
	if (btmtk_sdio_download_fw(card)) {
		pr_err("Downloading firmware failed!\n");
		fw_download_fail = 1;
	}

	/* Move from btmtk_fops_open() */
	spin_lock_init(&(metabuffer.spin_lock.lock));
	spin_lock_init(&(fwlog_metabuffer.spin_lock.lock));
	pr_debug("%s spin_lock_init end\n", __func__);

	priv = btmtk_add_card(card);
	if (!priv) {
		pr_err("Initializing card failed!\n");
		ret = -ENODEV;
		goto unreg_dev;
	}
	pr_debug("btmtk_add_card success\n");
	card->priv = priv;
	pr_debug("assign priv done\n");
	/* Initialize the interface specific function pointers */
	priv->hw_host_to_card = btmtk_sdio_host_to_card;
	priv->hw_process_int_status = btmtk_sdio_process_int_status;
	priv->hw_set_own_back =  btmtk_sdio_set_own_back;
	priv->hw_sdio_reset_dongle = btmtk_sdio_reset_dongle;
	priv->start_reset_dongle_progress = btmtk_sdio_start_reset_dongle_progress;
	g_priv = priv;

	memset(&metabuffer.buffer, 0, META_BUFFER_SIZE);


#if SAVE_FW_DUMP_IN_KERNEL
	fw_dump_file = NULL;
#endif

	ret = btmtk_sdio_readl(CHLPCR, &u32ReadCRValue);
	pr_debug("%s read CHLPCR (0x%08X)\n", __func__, u32ReadCRValue);
	pr_debug("%s chipid is  (0x%X)\n", __func__, g_card->chip_id);
	if (is_support_unify_woble(g_card)) {
		memset(g_card->bdaddr, 0, BD_ADDRESS_SIZE);
		btmtk_sdio_load_woble_setting(g_card->woble_setting,
			g_card->woble_setting_file_name,
			&g_card->func->dev,
			&g_card->woble_setting_len,
			g_card);
	}

#if (SUPPORT_UNIFY_WOBLE & SUPPORT_ANDROID)
	wake_lock_init(&g_card->woble_wlock, WAKE_LOCK_SUSPEND, "btevent_woble");
#endif

#if SUPPORT_EINT
	wake_lock_init(&g_card->eint_wlock, WAKE_LOCK_SUSPEND, "btevent_eint");
#endif

	pr_info("%s normal end\n", __func__);
	probe_ready = true;
	if (fw_download_fail)
		btmtk_sdio_start_reset_dongle_progress();

	return 0;

unreg_dev:
	btmtk_sdio_unregister_dev(card);

	pr_err("%s fail end\n", __func__);
	return ret;
}

/* The btmtk_sdio_remove() callback function is called
 * when user removes this module from kernel space or ejects
 * the card from the slot. The driver handles these 2 cases
 * differently.
 * If the user is removing the module, a MODULE_SHUTDOWN_REQ
 * command is sent to firmware and interrupt will be disabled.
 * If the card is removed, there is no need to send command
 * or disable interrupt.
 *
 * The variable 'user_rmmod' is used to distinguish these two
 * scenarios. This flag is initialized as FALSE in case the card
 * is removed, and will be set to TRUE for module removal when
 * module_exit function is called.
 */
static void btmtk_sdio_remove(struct sdio_func *func)
{
	struct btmtk_sdio_card *card;

	pr_info("%s begin user_rmmod %d\n", __func__, user_rmmod);
	probe_ready = false;
	btmtk_sdio_set_no_fw_own(g_priv, FALSE);
	if (func) {
		card = sdio_get_drvdata(func);
		if (card) {
			/* Send SHUTDOWN command & disable interrupt
			 * if user removes the module.
			 */
			if (user_rmmod) {
				pr_info("%s begin user_rmmod %d in user mode\n",
					__func__, user_rmmod);
				btmtk_sdio_set_own_back(DRIVER_OWN);
				btmtk_sdio_enable_interrupt(0);
				btmtk_sdio_bt_set_power(0);
				btmtk_sdio_set_own_back(FW_OWN);

				btmtk_sdio_disable_host_int(card);
			}
			btmtk_sdio_woble_free_setting();
			pr_debug("unregester dev\n");
			card->priv->surprise_removed = true;
			btmtk_sdio_unregister_dev(card);
			btmtk_remove_card(card->priv);
			need_reset_stack = 1;
		}
	}
	pr_info("%s end\n", __func__);
}

/*
 * cmd_type:
 * #define HCI_COMMAND_PKT   0x01
 * #define HCI_ACLDATA_PKT   0x02
 * #define HCI_SCODATA_PKT   0x03
 * #define HCI_EVENT_PKT     0x04
 * #define HCI_VENDOR_PKT    0xff
 */
static int btmtk_sdio_send_hci_cmd(u8 cmd_type, u8 *cmd, int cmd_len,
		const u8 *event, const int event_len,
		int total_timeout, bool wait_until)
		/*cmd: if cmd is null, don't compare event, just return 0 if send cmd success*/
		/* total_timeout: -1 */
		/* add_spec_header:0 hci event, 1 use vend specic event header*/
		/* return 0 if compare successfully and no need to compare */
		/* return < 0 if error*/
		/*wait_until: 0:need compare with first event after cmd*/
		/*return value: 0 or positive success, -x fail*/
{
	int ret = -1;
	unsigned long comp_event_timo = 0, start_time = 0;
	struct sk_buff *skb = NULL;

	if (cmd_len == 0) {
		pr_err("%s cmd_len (%d) error return\n", __func__, cmd_len);
		return -EINVAL;
	}


	skb = bt_skb_alloc(cmd_len, GFP_ATOMIC);
	bt_cb(skb)->pkt_type = cmd_type;
	memcpy(&skb->data[0], cmd, cmd_len);
	skb->len = cmd_len;
	if (event) {
		event_compare_status = BTMTK_SDIO_EVENT_COMPARE_STATE_NEED_COMPARE;
		memcpy(event_need_compare, event, event_len);
		event_need_compare_len = event_len;
	}
	skb_queue_tail(&g_priv->adapter->tx_queue, skb);
	wake_up_interruptible(&g_priv->main_thread.wait_q);


	if (event == NULL)
		return 0;

	if (event_len > EVENT_COMPARE_SIZE) {
		pr_err("%s event_len (%d) > EVENT_COMPARE_SIZE(%d), error\n", __func__, event_len, EVENT_COMPARE_SIZE);
		return -1;
	}

	start_time = jiffies;
	/* check HCI event */
	comp_event_timo = jiffies + msecs_to_jiffies(total_timeout);
	ret = -1;
	pr_debug("%s event_need_compare_len %d\n", __func__, event_need_compare_len);
	pr_debug("%s event_compare_status %d\n", __func__, event_compare_status);
	do {
		/* check if event_compare_success */
		if (event_compare_status == BTMTK_SDIO_EVENT_COMPARE_STATE_COMPARE_SUCCESS) {
			pr_debug("%s compare success\n", __func__);
			ret = 0;
			break;
		}

		msleep(100);
	} while (time_before(jiffies, comp_event_timo));
	event_compare_status = BTMTK_SDIO_EVENT_COMPARE_STATE_NOTHING_NEED_COMPARE;
	pr_debug("%s ret %d\n", __func__, ret);
	return ret;
}

static int btmtk_sdio_trigger_fw_assert(void)
{
	int ret = 0;
	u8 cmd[] = { 0x5b, 0xfd, 0x00 };

	pr_info("%s begin\n", __func__);
	ret = btmtk_sdio_send_hci_cmd(HCI_COMMAND_PKT, cmd,
		sizeof(cmd),
		NULL, 0, WOBLE_COMP_EVENT_TIMO, 1);
	if (ret != 0)
		pr_info("%s ret = %d\n", __func__, ret);
	return ret;
}

static int btmtk_sdio_send_set_i2s_slave(void)
{
	int ret = 0;
	u8 comp_event[] = {0xE, 0x04, 0x01, 0x72, 0xFC, 0x00 };
	u8 cmd[] = { 0x72, 0xFC, 4, 03, 0x10, 0x00, 0x02 };

	ret = btmtk_sdio_send_hci_cmd(HCI_COMMAND_PKT, cmd,
		sizeof(cmd),
		comp_event, sizeof(comp_event), WOBLE_COMP_EVENT_TIMO, 1);
	if (ret != 0)
		pr_info("%s ret = %d\n", __func__, ret);
	return ret;
}

static int btmtk_sdio_send_get_vendor_cap(void)
{
	int ret = -1;
	u8 get_vendor_cap_cmd[] = { 0x53, 0xFD, 0x00 };
	u8 get_vendor_cap_event[] = { 0x0e, 0x12, 0x01, 0x53, 0xFD, 0x00};

	pr_debug("%s: begin", __func__);
	BTSDIO_DEBUG_RAW(get_vendor_cap_cmd, sizeof(get_vendor_cap_cmd), "%s: send vendor_cap_cmd is:", __func__);
	ret = btmtk_sdio_send_hci_cmd(HCI_COMMAND_PKT, get_vendor_cap_cmd, sizeof(get_vendor_cap_cmd),
		get_vendor_cap_event, sizeof(get_vendor_cap_event),
				WOBLE_COMP_EVENT_TIMO, 1);

	pr_debug("%s: ret %d", __func__, ret);
	return ret;
}

static int btmtk_sdio_send_read_BDADDR_cmd(void)
{
	u8 cmd[] = { 0x09, 0x10, 0x00 };
	int ret = -1;
	unsigned char zero[BD_ADDRESS_SIZE];

	pr_debug("%s: begin", __func__);
	if (g_card == NULL) {
		pr_err("%s: g_card == NULL!", __func__);
		return -1;
	}

	memset(zero, 0, sizeof(zero));
	if (memcmp(g_card->bdaddr, zero, BD_ADDRESS_SIZE) != 0) {
		pr_debug("%s: already got bdaddr %02x%02x%02x%02x%02x%02x, return 0", __func__,
		g_card->bdaddr[0], g_card->bdaddr[1], g_card->bdaddr[2],
		g_card->bdaddr[3], g_card->bdaddr[4], g_card->bdaddr[5]);
		return 0;
	}
	BTSDIO_DEBUG_RAW(cmd, sizeof(cmd), "%s: send read bd address cmd is:", __func__);
	ret = btmtk_sdio_send_hci_cmd(HCI_COMMAND_PKT, cmd, sizeof(cmd),
		READ_ADDRESS_EVENT, sizeof(READ_ADDRESS_EVENT), WOBLE_COMP_EVENT_TIMO, 1);
	/*BD address will get in btmtk_sdio_host_to_card*/
	pr_debug("%s: ret = %d", __func__, ret);

	return ret;
}

static int btmtk_sdio_set_Woble_APCF_filter_parameter(void)
{
	int ret = -1;
	u8 cmd[] = { 0x57, 0xfd, 0x0a, 0x01, 0x00, 0x5a, 0x20, 0x00, 0x20, 0x00, 0x01, 0x80, 0x00 };
	u8 event_complete[] = { 0x0e, 0x07, 0x01, 0x57, 0xfd, 0x00, 0x01/*, 00, 63*/ };

	pr_debug("%s: begin", __func__);
	ret = btmtk_sdio_send_hci_cmd(HCI_COMMAND_PKT, cmd, sizeof(cmd),
		event_complete, sizeof(event_complete),
		WOBLE_COMP_EVENT_TIMO, 1);
	if (ret < 0)
		pr_err("%s: end ret %d", __func__, ret);
	else
		ret = 0;

	pr_info("%s: end ret=%d", __func__, ret);
	return ret;
}


/**
 * Set APCF manufacturer data and filter parameter
 */
static int btmtk_sdio_set_Woble_APCF(void)
{
	int ret = -1;
	int i = 0;
	u8 manufactur_data[] = { 0x57, 0xfd, 0x27, 0x06, 0x00, 0x5a,
		0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x43, 0x52, 0x4B, 0x54, 0x4D,
		0xFF, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	u8 event_complete[] = { 0x0e, 0x07, 0x01, 0x57, 0xfd};

	pr_debug("%s: begin", __func__);
	if (!g_card) {
		pr_info("%s: g_card is NULL, return -1", __func__);
		return -1;
	}

	pr_debug("%s: g_card->woble_setting_apcf[0].length %d",
		__func__, g_card->woble_setting_apcf[0].length);

	/* start to send apcf cmd from woble setting  file */
	if (g_card->woble_setting_apcf[0].length) {
		for (i = 0; i < WOBLE_SETTING_COUNT; i++) {
			if (!g_card->woble_setting_apcf[i].length)
				continue;

			pr_info("%s: g_data->woble_setting_apcf_fill_mac[%d].content[0] = 0x%02x",
				__func__, i,
				g_card->woble_setting_apcf_fill_mac[i].content[0]);
			pr_info("%s: g_data->woble_setting_apcf_fill_mac_location[%d].length = %d",
				__func__, i,
				g_card->woble_setting_apcf_fill_mac_location[i].length);

			if ((g_card->woble_setting_apcf_fill_mac[i].content[0] == 1) &&
				g_card->woble_setting_apcf_fill_mac_location[i].length) {
				/* need add BD addr to apcf cmd */
				memcpy(g_card->woble_setting_apcf[i].content +
					(*g_card->woble_setting_apcf_fill_mac_location[i].content),
					g_card->bdaddr, BD_ADDRESS_SIZE);
				pr_info("%s: apcf %d ,add mac to location %d",
					__func__, i,
					(*g_card->woble_setting_apcf_fill_mac_location[i].content));
			}

			pr_info("%s: send APCF %d", __func__, i);
			BTSDIO_INFO_RAW(g_card->woble_setting_apcf[i].content, g_card->woble_setting_apcf[i].length,
				"woble_setting_apcf");

			ret = btmtk_sdio_send_hci_cmd(HCI_COMMAND_PKT, g_card->woble_setting_apcf[i].content,
				g_card->woble_setting_apcf[i].length,
				event_complete, sizeof(event_complete), WOBLE_COMP_EVENT_TIMO, 1);

			if (ret < 0) {
				pr_err("%s: apcf %d error ret %d", __func__, i, ret);
				return ret;
			}

		}
	} else { /* use default */
		pr_info("%s: use default manufactur data", __func__);
		memcpy(manufactur_data + 9, g_card->bdaddr, BD_ADDRESS_SIZE);
		BTSDIO_DEBUG_RAW(manufactur_data, sizeof(manufactur_data),
						"send manufactur_data ");

		ret = btmtk_sdio_send_hci_cmd(HCI_COMMAND_PKT, manufactur_data,
				sizeof(manufactur_data),
				event_complete, sizeof(event_complete), WOBLE_COMP_EVENT_TIMO, 1);
		if (ret < 0) {
			pr_err("%s: manufactur_data error ret %d", __func__, ret);
			return ret;
		}

		ret = btmtk_sdio_set_Woble_APCF_filter_parameter();
	}

	pr_info("%s: end ret=%d", __func__, ret);
	return ret;
}



static int btmtk_sdio_send_woble_settings(struct woble_setting_struct *settings_cmd,
	struct woble_setting_struct *settings_event, char *message)
{
	int ret = -1;
	int i = 0;

	pr_info("%s: %s length %d",
		__func__, message, settings_cmd->length);
	if (g_card->woble_setting_radio_on[0].length) {
		for (i = 0; i < WOBLE_SETTING_COUNT; i++) {
			if (settings_cmd[i].length) {
				pr_info("%s: send %s %d", __func__, message, i);
				BTSDIO_INFO_RAW(settings_cmd[i].content,
					settings_cmd[i].length, "Raw");

				ret = btmtk_sdio_send_hci_cmd(HCI_COMMAND_PKT, settings_cmd[i].content,
				settings_cmd[i].length,
				settings_event[i].content,
				settings_event[i].length, WOBLE_COMP_EVENT_TIMO, 1);

				if (ret) {
					pr_err("%s: %s %d return error",
							__func__, message, i);
					return ret;
				}
			}
		}
	}
	return ret;
}
static int btmtk_sdio_send_unify_woble_suspend_default_cmd(void)
{
	int ret = 0;	/* if successful, 0 */
	u8 cmd[] = { 0xC9, 0xFC, 0x14, 0x01, 0x20, 0x02, 0x00, 0x01,
		0x02, 0x01, 0x00, 0x05, 0x10, 0x01, 0x00, 0x40, 0x06,
		0x02, 0x40, 0x5A, 0x02, 0x41, 0x0F };
	/*u8 status[] = { 0x0F, 0x04, 0x00, 0x01, 0xC9, 0xFC };*/
	u8 comp_event[] = { 0xE6, 0x02, 0x08, 0x00 };

	pr_debug("%s: begin", __func__);

	ret = btmtk_sdio_send_hci_cmd(HCI_COMMAND_PKT, cmd,
				sizeof(cmd),
				comp_event, sizeof(comp_event), WOBLE_COMP_EVENT_TIMO, 1);
	if (ret)
		pr_err("%s: comp_event return error(%d)", __func__, ret);

	return ret;
}
/**
 * Set APCF manufacturer data and filter parameter
 */
static int btmtk_sdio_set_Woble_radio_off(void)
{
	int ret = -1;

	pr_debug("%s: g_data->woble_setting_radio_off[0].length %d",
			__func__, g_card->woble_setting_radio_off[0].length);
	pr_debug("%s: g_card->woble_setting_radio_off_comp_event[0].length %d",
			__func__, g_card->woble_setting_radio_off_comp_event[0].length);

	if (g_card->woble_setting_radio_off[0].length) {
		if (g_card->woble_setting_radio_off_comp_event[0].length &&
		is_support_unify_woble(g_card)) {
			ret = btmtk_sdio_send_woble_settings(g_card->woble_setting_radio_off,
			g_card->woble_setting_radio_off_comp_event, "radio off");
			if (ret) {
				pr_err("%s: radio off error", __func__);
				return ret;
			}
		} else
			pr_info("%s: woble_setting_radio_off length is %d", __func__,
				g_card->woble_setting_radio_off[0].length);
	} else {/* use default */
		pr_info("%s: use default radio off cmd", __func__);
		ret = btmtk_sdio_send_unify_woble_suspend_default_cmd();
	}

	pr_info("%s: end ret=%d", __func__, ret);
	return ret;
}

static int btmtk_sdio_handle_entering_WoBLE_state(void)
{
	int ret = -1;

	pr_debug("%s: begin", __func__);
	if (is_support_unify_woble(g_card)) {
		ret = btmtk_sdio_send_get_vendor_cap();
		if (ret < 0) {
			pr_err("%s: btmtk_sdio_send_get_vendor_cap return fail ret = %d", __func__, ret);
			goto Finish;
		}

		ret = btmtk_sdio_send_read_BDADDR_cmd();
		if (ret < 0) {
			pr_err("%s: btmtk_sdio_send_read_BDADDR_cmd return fail ret = %d", __func__, ret);
			goto Finish;
		}

		ret = btmtk_sdio_set_Woble_APCF();
		if (ret < 0) {
			pr_err("%s: btmtk_sdio_set_Woble_APCF return fail %d", __func__, ret);
			goto Finish;
		}

		ret = btmtk_sdio_set_Woble_radio_off();
		if (ret < 0) {
			pr_err("%s: btmtk_sdio_set_Woble_radio_off return fail %d", __func__, ret);
			goto Finish;
		}
	}

Finish:
	if (ret)
		btmtk_sdio_woble_wake_lock(g_card);

	pr_info("%s: end", __func__);
	return ret;
}

static int btmtk_sdio_send_leave_woble_suspend_cmd(void)
{
	int ret = 0;	/* if successful, 0 */
	u8 cmd[] = { 0xC9, 0xFC, 0x05, 0x01, 0x21, 0x02, 0x00, 0x00 };
	u8 comp_event[] = { 0xe6, 0x02, 0x08, 0x01 };

	BTSDIO_DEBUG_RAW(cmd, sizeof(cmd), "cmd ");
	ret = btmtk_sdio_send_hci_cmd(HCI_COMMAND_PKT, cmd,	sizeof(cmd),
				comp_event, sizeof(comp_event), WOBLE_COMP_EVENT_TIMO, 1);

	if (ret < 0) {
		pr_err("%s: failed(%d)", __func__, ret);
	} else {
		pr_info("%s: OK", __func__);
		ret = 0;
	}
	return ret;
}
static int btmtk_sdio_del_Woble_APCF_inde(void)
{
	int ret = -1;
	u8 cmd[] = { 0x57, 0xfd, 0x03, 0x01, 0x01, 0x5a };
	u8 event[] = { 0x0e, 0x07, 0x01, 0x57, 0xfd, 0x00, 0x01, /* 00, 63 */ };

	pr_debug("%s", __func__);
	ret = btmtk_sdio_send_hci_cmd(HCI_COMMAND_PKT, cmd, sizeof(cmd),
		event, sizeof(event), WOBLE_COMP_EVENT_TIMO, 1);

	if (ret < 0)
		pr_err("%s: Got error %d", __func__, ret);

	pr_info("%s end ret = %d", __func__, ret);
	return ret;
}

static int btmtk_sdio_handle_leaving_WoBLE_state(void)
{
	int ret = -1;

	if (g_card == NULL) {
		pr_err("%s: g_card is NULL return", __func__);
		return 0;
	}

	if (!is_support_unify_woble(g_card)) {
		pr_err("%s: do nothing", __func__);
		return 0;
	}

	if (g_card->woble_setting_radio_on[0].length &&
		g_card->woble_setting_radio_on_comp_event[0].length &&
		g_card->woble_setting_apcf_resume[0].length &&
		g_card->woble_setting_apcf_resume_event[0].length) {
			/* start to send radio off cmd from woble setting file */
		ret = btmtk_sdio_send_woble_settings(g_card->woble_setting_radio_on,
			g_card->woble_setting_radio_on_comp_event, "radio on");
		if (ret) {
			pr_err("%s: wradio on error", __func__);
			return ret;
		}

		ret = btmtk_sdio_send_woble_settings(g_card->woble_setting_apcf_resume,
			g_card->woble_setting_apcf_resume_event, "apcf resume");
		if (ret) {
			pr_err("%s: apcf resume error", __func__);
			return ret;
		}

	} else { /* use default */
		ret = btmtk_sdio_send_leave_woble_suspend_cmd();
		if (ret) {
			pr_err("%s: radio on error", __func__);
			return ret;
		}

		ret = btmtk_sdio_del_Woble_APCF_inde();
		if (ret) {
			pr_err("%s: del apcf index error", __func__);
			return ret;
		}
	}

	if (ret) {
		pr_err("%s: woble_setting_radio_on return error", __func__);
		return ret;
	}



	return ret;
}

static int btmtk_sdio_send_apcf_reserved(void)
{
	int ret = -1;
	u8 reserve_apcf_cmd[] = { 0x5C, 0xFC, 0x01, 0x0A };
	u8 reserve_apcf_event[] = { 0x0e, 0x06, 0x01, 0x5C, 0xFC, 0x00 };

	ret = btmtk_sdio_send_hci_cmd(HCI_COMMAND_PKT, reserve_apcf_cmd,
				sizeof(reserve_apcf_cmd),
				reserve_apcf_event, sizeof(reserve_apcf_event), WOBLE_COMP_EVENT_TIMO, 1);

	pr_info("%s: ret %d", __func__, ret);
	return ret;
}

static int btmtk_sdio_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	u8 ret = 0;
	mmc_pm_flag_t pm_flags;

	pr_info("%s begin\n", __func__);

	if (g_card == NULL) {
		pr_err("%s: g_card is NULL return", __func__);
		return 0;
	}

	if ((g_card->suspend_count++)) {
		pr_warn("%s: Has suspended. suspend_count: %d", __func__, g_card->suspend_count);
		pr_info("%s: end", __func__);
		return 0;
	}
	pr_debug("%s start to send DRIVER_OWN\n", __func__);
	ret = btmtk_sdio_set_own_back(DRIVER_OWN);

	if (ret)
		pr_err("%s set driver own fail\n", __func__);

	if (!is_support_unify_woble(g_card))
		pr_warn("%s: no support", __func__);
	else
		btmtk_sdio_handle_entering_WoBLE_state();

#if SUPPORT_EINT
	if (g_card->wobt_irq != 0 && atomic_read(&(g_card->irq_enable_count)) == 0) {
		pr_info("%s:enable BT IRQ:%d\n", __func__, g_card->wobt_irq);
		irq_set_irq_wake(g_card->wobt_irq, 1);
		enable_irq(g_card->wobt_irq);
		atomic_inc(&(g_card->irq_enable_count));
	} else
		pr_info("%s:irq_enable count:%d\n", __func__, atomic_read(&(g_card->irq_enable_count)));
#endif

	if (func) {
		pm_flags = sdio_get_host_pm_caps(func);
		pr_debug("%s: suspend: PM flags = 0x%x\n",
			sdio_func_id(func), pm_flags);
		if (!(pm_flags & MMC_PM_KEEP_POWER)) {
			pr_err("%s: cannot remain alive while suspended\n",
				sdio_func_id(func));
			return -EINVAL;
		}
	} else {
		pr_err("sdio_func is not specified\n");
		return 0;
	}

	ret = btmtk_sdio_set_own_back(FW_OWN);
	if (ret)
		pr_err("%s set fw own fail\n", __func__);

	return sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
}

static int btmtk_sdio_resume(struct device *dev)
{
	u8 ret = 0;

	if (g_card == NULL) {
		pr_err("%s: g_card is NULL return", __func__);
		return 0;
	}

	g_card->suspend_count--;
	if (g_card->suspend_count) {
		pr_info("%s: data->suspend_count %d, return 0", __func__, g_card->suspend_count);
		return 0;
	}
#if SUPPORT_EINT
	if (g_card->wobt_irq != 0 && atomic_read(&(g_card->irq_enable_count)) == 1) {
		pr_info("%s:disable BT IRQ:%d\n", __func__, g_card->wobt_irq);
		atomic_dec(&(g_card->irq_enable_count));
		disable_irq_nosync(g_card->wobt_irq);
	} else
		pr_info("%s:irq_enable count:%d\n", __func__, atomic_read(&(g_card->irq_enable_count)));
#endif
	ret = btmtk_sdio_handle_leaving_WoBLE_state();

	if (ret)
		pr_err("%s: btmtk_sdio_handle_leaving_WoBLE_state return fail  %d", __func__, ret);

	pr_info("%s end\n", __func__);
	return 0;
}

static const struct dev_pm_ops btmtk_sdio_pm_ops = {
	.suspend       = btmtk_sdio_suspend,
	.resume        = btmtk_sdio_resume,
};

static struct sdio_driver bt_mtk_sdio = {
	.name          = "btmtk_sdio",
	.id_table      = btmtk_sdio_ids,
	.probe         = btmtk_sdio_probe,
	.remove        = btmtk_sdio_remove,
	.drv = {
		.owner = THIS_MODULE,
		.pm = &btmtk_sdio_pm_ops,
	}
};

static int btmtk_sdio_send_hci_reset(void)
{
	int ret = 0;
	u8 comp_event[] = {0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00 };
	u8 cmd[] = { 0x03, 0x0c, 0x00 };

	pr_info("%s begin\n", __func__);
	ret = btmtk_sdio_send_hci_cmd(HCI_COMMAND_PKT, cmd,
		sizeof(cmd),
		comp_event, sizeof(comp_event), WOBLE_COMP_EVENT_TIMO, 1);
	if (ret != 0)
		pr_info("%s ret = %d\n", __func__, ret);
	return ret;
}

static int btmtk_clean_queue(void)
{
	struct sk_buff *skb = NULL;

	LOCK_UNSLEEPABLE_LOCK(&(metabuffer.spin_lock));
	if (!skb_queue_empty(&g_priv->adapter->fops_queue)) {
		pr_info("%s clean data in fops_queue\n", __func__);
		do {
			skb = skb_dequeue(&g_priv->adapter->fops_queue);
			if (skb == NULL) {
				pr_info("%s skb=NULL error break\n", __func__);
				break;
			}

			kfree_skb(skb);
		} while (!skb_queue_empty(&g_priv->adapter->fops_queue));
	}
	UNLOCK_UNSLEEPABLE_LOCK(&(metabuffer.spin_lock));
	return 0;
}

static int btmtk_fops_open(struct inode *inode, struct file *file)
{
	pr_info("%s begin\n", __func__);

	if (!probe_ready) {
		pr_err("%s probe_ready is %d return\n",
			__func__, probe_ready);
		return -EFAULT;
	}

	metabuffer.read_p = 0;
	metabuffer.write_p = 0;
#if 0 /* Move to btmtk_sdio_probe() */
	spin_lock_init(&(metabuffer.spin_lock.lock));
	pr_info("%s spin_lock_init end\n", __func__);
#endif
	if (g_priv == NULL) {
		pr_err("%s g_priv is NULL\n", __func__);
		return -ENOENT;
	}

	if (g_priv->adapter == NULL) {
		pr_err("%s g_priv->adapter is NULL\n", __func__);
		return -ENOENT;
	}

	if (g_card) {
		if (is_support_unify_woble(g_card))
			btmtk_sdio_send_apcf_reserved();
	} else
		pr_err("%s g_card is NULL\n", __func__);

	btmtk_clean_queue();

	if (g_priv)
		g_priv->adapter->fops_mode = true;

	need_reset_stack = 0;
	need_reopen = 0;
	pr_info("%s fops_mode=%d end\n", __func__, g_priv->adapter->fops_mode);
	return 0;
}

static int btmtk_fops_close(struct inode *inode, struct file *file)
{
	pr_info("%s begin\n", __func__);

	if (!probe_ready) {
		pr_err("%s probe_ready is %d return\n",
			__func__, probe_ready);
		return -EFAULT;
	}

	if (g_priv)
		g_priv->adapter->fops_mode = false;

	/* for VTS loopback mode test */
	btmtk_sdio_send_hci_reset();
	btmtk_clean_queue();
	need_reopen = 0;
	pr_info("%s fops_mode=%d end\n", __func__, g_priv->adapter->fops_mode);
	return 0;
}

ssize_t btmtk_fops_write(struct file *filp, const char __user *buf,
			size_t count, loff_t *f_pos)
{
	int retval = 0;
	struct sk_buff *skb = NULL;
	u32 crAddr = 0, crValue = 0, crMask = 0;
	static u8 waiting_for_hci_without_packet_type; /* INITIALISED_STATIC: do not initialise statics to 0 */
	static u8 hci_packet_type = 0xff;
	u32 copy_size = 0;
	unsigned long c_result = 0;

	if (!probe_ready) {
		pr_err("%s probe_ready is %d return\n",
			__func__, probe_ready);
		return -EFAULT;
	}

	if (g_priv == NULL) {
		pr_info("%s g_priv is NULL\n", __func__);
		return -EFAULT;
	}

	if (g_priv->adapter->fops_mode == 0) {
		pr_info("%s fops_mode is 0\n", __func__);
		return -EFAULT;
	}

	if (need_reopen) {
		pr_info("%s: need_reopen (%d)!", __func__, need_reopen);
		return -EFAULT;
	}
#if 0
	pr_info("%s : (%d) %02X %02X %02X %02X "
			%"02X %02X %02X %02X\n",
			__func__, (int)count,
			buf[0], buf[1], buf[2], buf[3],
			buf[4], buf[5], buf[6], buf[7]);

	pr_info("%s print write data", __func__);
	if (count > 10)
		pr_info("  %02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
				buf[0], buf[1], buf[2], buf[3], buf[4],
				buf[5], buf[6], buf[7], buf[8], buf[9]);
	else {
		for (i = 0; i < count; i++)
			pr_info("%d %02X", i, buf[i]);
	}
#endif
	if (count > 0) {
		memset(userbuf, 0, MTK_TXDATA_SIZE);
		c_result = copy_from_user(userbuf, buf, count);
	} else {
		pr_err("%s: target packet length:%zu is not allowed", __func__, count);
		return -EFAULT;
	}

	if (c_result != 0) {
		pr_err("%s copy_from_user failed!\n", __func__);
		return -EFAULT;
	}

	if (userbuf[0] == 0x7 && waiting_for_hci_without_packet_type == 0) {
		/* write CR */
		if (count < 15) {
			pr_info("%s count=%zd less than 15, error\n",
				__func__, count);
			return -EFAULT;
		}

		crAddr = (userbuf[3]&0xff) + ((userbuf[4]&0xff)<<8)
			+ ((userbuf[5]&0xff)<<16) + ((userbuf[6]&0xff)<<24);
		crValue = (userbuf[7]&0xff) + ((userbuf[8]&0xff)<<8)
			+ ((userbuf[9]&0xff)<<16) + ((userbuf[10]&0xff)<<24);
		crMask = (userbuf[11]&0xff) + ((userbuf[12]&0xff)<<8)
			+ ((userbuf[13]&0xff)<<16) + ((userbuf[14]&0xff)<<24);

		pr_info("%s crAddr=0x%08x crValue=0x%08x crMask=0x%08x\n",
			__func__, crAddr, crValue, crMask);
		crValue &= crMask;

		pr_info("%s write crAddr=0x%08x crValue=0x%08x\n", __func__,
			crAddr, crValue);
		btmtk_sdio_writel(crAddr, crValue);
		retval = count;
	} else if (userbuf[0] == 0x8 && waiting_for_hci_without_packet_type == 0) {
		/* read CR */
		if (count < 16) {
			pr_info("%s count=%zd less than 15, error\n",
				__func__, count);
			return -EFAULT;
		}

		crAddr = (userbuf[3]&0xff) + ((userbuf[4]&0xff)<<8) +
			((userbuf[5]&0xff)<<16) + ((userbuf[6]&0xff)<<24);
		crMask = (userbuf[11]&0xff) + ((userbuf[12]&0xff)<<8) +
			((userbuf[13]&0xff)<<16) + ((userbuf[14]&0xff)<<24);

		btmtk_sdio_readl(crAddr, &crValue);
		pr_info("%s read crAddr=0x%08x crValue=0x%08x crMask=0x%08x\n",
				__func__, crAddr, crValue, crMask);
		retval = count;
	} else {
		if (waiting_for_hci_without_packet_type == 1 && count == 1) {
			pr_warn("%s: Waiting for hci_without_packet_type, but receive data count is 1!", __func__);
			pr_warn("%s: Treat this packet as packet_type", __func__);
			memcpy(&hci_packet_type, &userbuf[0], 1);
			waiting_for_hci_without_packet_type = 1;
			retval = 1;
			goto OUT;
		}

		if (waiting_for_hci_without_packet_type == 0) {
			if (count == 1) {
				memcpy(&hci_packet_type, &userbuf[0], 1);
				waiting_for_hci_without_packet_type = 1;
				retval = 1;
				goto OUT;
			}
		}

		if (waiting_for_hci_without_packet_type) {
			copy_size = count + 1;
			skb = bt_skb_alloc(copy_size-1, GFP_ATOMIC);
			if (skb == NULL) {
				pr_warn("skb is null\n");
				retval = -ENOMEM;
				goto OUT;
			}
			bt_cb(skb)->pkt_type = hci_packet_type;
			memcpy(&skb->data[0], &userbuf[0], copy_size-1);
		} else {
			copy_size = count;
			skb = bt_skb_alloc(copy_size-1, GFP_ATOMIC);
			if (skb == NULL) {
				pr_warn("skb is null\n");
				retval = -ENOMEM;
				goto OUT;
			}
			bt_cb(skb)->pkt_type = userbuf[0];
			memcpy(&skb->data[0], &userbuf[1], copy_size-1);
		}

		skb->len = copy_size-1;
		skb_queue_tail(&g_priv->adapter->tx_queue, skb);

		if (bt_cb(skb)->pkt_type == HCI_COMMAND_PKT) {
			u8 fw_assert_cmd[] = { 0x6F, 0xFC, 0x05, 0x01, 0x02, 0x01, 0x00, 0x08 };
			u8 reset_cmd[] = { 0x03, 0x0C, 0x00 };
			u8 read_ver_cmd[] = { 0x01, 0x10, 0x00 };

			if (skb->len == sizeof(fw_assert_cmd) &&
				!memcmp(&skb->data[0], fw_assert_cmd, sizeof(fw_assert_cmd)))
				pr_info("%s: Donge FW Assert Triggered by upper layer\n", __func__);
			else if (skb->len == sizeof(reset_cmd) &&
				!memcmp(&skb->data[0], reset_cmd, sizeof(reset_cmd)))
				pr_info("%s: got command: 0x03 0C 00 (HCI_RESET)\n", __func__);
			else if (skb->len == sizeof(read_ver_cmd) &&
				!memcmp(&skb->data[0], read_ver_cmd, sizeof(read_ver_cmd)))
				pr_info("%s: got command: 0x01 10 00 (READ_LOCAL_VERSION)\n", __func__);
		}

		wake_up_interruptible(&g_priv->main_thread.wait_q);

		retval = copy_size;

		if (waiting_for_hci_without_packet_type) {
			hci_packet_type = 0xff;
			waiting_for_hci_without_packet_type = 0;
			if (retval > 0)
				retval -= 1;
		}
	}

OUT:

	pr_debug("%s end\n", __func__);
	return retval;
}

ssize_t btmtk_fops_read(struct file *filp, char __user *buf,
			size_t count, loff_t *f_pos)
{
	struct sk_buff *skb = NULL;
	int copyLen = 0;
	u8 hwerr_event[] = { 0x04, 0x10, 0x01, 0xff };
	static int send_hw_err_event_count;

	if (!probe_ready) {
		pr_err("%s probe_ready is %d return\n",
			__func__, probe_ready);
		return -EFAULT;
	}

	if (g_priv == NULL) {
		pr_info("%s g_priv is NULL\n", __func__);
		return -EFAULT;
	}

	if (g_priv->adapter->fops_mode == 0) {
		pr_info("%s fops_mode is 0\n", __func__);
		return -EFAULT;
	}

	if (need_reset_stack == 1) {
		pr_warn("%s: Reset BT stack", __func__);
		pr_warn("%s: go if send_hw_err_event_count %d", __func__, send_hw_err_event_count);
		if (send_hw_err_event_count < sizeof(hwerr_event)) {
			if (count < (sizeof(hwerr_event) - send_hw_err_event_count)) {
				copyLen = count;
				pr_info("call wake_up_interruptible");
				wake_up_interruptible(&inq);
			} else
				copyLen = (sizeof(hwerr_event) - send_hw_err_event_count);
			pr_warn("%s: in if copyLen = %d", __func__, copyLen);
			if (copy_to_user(buf, hwerr_event + send_hw_err_event_count, copyLen)) {
				pr_err("send_hw_err_event_count %d copy to user fail, count = %d, go out",
					send_hw_err_event_count, copyLen);
				copyLen = -EFAULT;
				goto OUT;
			}
			send_hw_err_event_count += copyLen;
			pr_warn("%s: in if send_hw_err_event_count = %d", __func__, send_hw_err_event_count);
			if (send_hw_err_event_count >= sizeof(hwerr_event)) {
				send_hw_err_event_count  = 0;
				pr_warn("%s: set need_reset_stack=0", __func__);
				need_reset_stack = 0;
				need_reopen = 1;
			}
			pr_warn("%s: set call up", __func__);
			goto OUT;
		} else {
			pr_warn("%s: xx set copyLen = -EFAULT", __func__);
			copyLen = -EFAULT;
			goto OUT;
		}
	}

	if (need_set_i2s == 1) {
		pr_info("%s get reset complete and set I2S !\n", __func__);
		btmtk_sdio_send_set_i2s_slave();
		need_set_i2s = 0;
	}

	LOCK_UNSLEEPABLE_LOCK(&(metabuffer.spin_lock));
	if (skb_queue_empty(&g_priv->adapter->fops_queue)) {
		/* if (filp->f_flags & O_NONBLOCK) { */
		if (metabuffer.write_p == metabuffer.read_p) {
			UNLOCK_UNSLEEPABLE_LOCK(&(metabuffer.spin_lock));
			return 0;
		}
	}

	do {
		skb = skb_dequeue(&g_priv->adapter->fops_queue);
		if (skb == NULL) {
			pr_debug("%s skb=NULL break\n", __func__);
			break;
		}

#if 0
		pr_debug("%s pkt_type %d metabuffer.buffer %d",
			__func__, bt_cb(skb)->pkt_type,
			metabuffer.buffer[copyLen]);
#endif
		btmtk_print_buffer_conent(skb->data, skb->len);

		if (btmtk_sdio_push_data_to_metabuffer(&metabuffer, skb->data,
				skb->len, bt_cb(skb)->pkt_type, true) < 0) {
			skb_queue_head(&g_priv->adapter->fops_queue, skb);
			break;
		}
		kfree_skb(skb);
	} while (!skb_queue_empty(&g_priv->adapter->fops_queue));
	UNLOCK_UNSLEEPABLE_LOCK(&(metabuffer.spin_lock));

	return btmtk_sdio_pull_data_from_metabuffer(&metabuffer, buf, count);

OUT:
	return copyLen;
}

static int btmtk_fops_fasync(int fd, struct file *file, int on)
{
	pr_info("%s: fd = 0x%X, flag = 0x%X\n", __func__, fd, on);
	return fasync_helper(fd, file, on, &fasync);
}

unsigned int btmtk_fops_poll(struct file *filp, poll_table *wait)
{
	unsigned int mask = 0;

	if (!probe_ready) {
		pr_err("%s probe_ready is %d return\n",
			__func__, probe_ready);
		return mask;
	}

	if (g_priv == NULL) {
		pr_err("%s g_priv is NULL\n", __func__);
		return -ENODEV;
	}

	if (metabuffer.write_p != metabuffer.read_p)
		mask |= (POLLIN | POLLRDNORM);

	if (skb_queue_empty(&g_priv->adapter->fops_queue)) {
		poll_wait(filp, &inq, wait);

		if (!skb_queue_empty(&g_priv->adapter->fops_queue)) {
			mask |= (POLLIN | POLLRDNORM);
			/* pr_info("%s poll done\n", __func__); */
		}
	} else
		mask |= (POLLIN | POLLRDNORM);

	mask |= (POLLOUT | POLLWRNORM);

	/* pr_info("%s poll mask 0x%x\n", __func__,mask); */
	return mask;
}

long btmtk_fops_unlocked_ioctl(struct file *filp,
				unsigned int cmd, unsigned long arg)
{
	u32 retval = 0;

	return retval;
}

static int btmtk_fops_openfwlog(struct inode *inode,
					struct file *file)
{
	if (g_priv == NULL) {
		pr_err("%s: ERROR, g_data is NULL!\n", __func__);
		return -ENODEV;
	}

	pr_info("%s: OK\n", __func__);
	return 0;
}

static int btmtk_fops_closefwlog(struct inode *inode,
					struct file *file)
{
	if (g_priv == NULL) {
		pr_err("%s: ERROR, g_data is NULL!\n", __func__);
		return -ENODEV;
	}

	pr_info("%s: OK\n", __func__);
	return 0;
}

static ssize_t btmtk_fops_readfwlog(struct file *filp,
			char __user *buf,
			size_t count,
			loff_t *f_pos)
{
	struct sk_buff *skb = NULL;

	if (!probe_ready) {
		pr_err("%s probe_ready is %d return\n",
			__func__, probe_ready);
		return -EFAULT;
	}

	if (g_priv == NULL) {
		pr_info("%s g_priv is NULL\n", __func__);
		return -EFAULT;
	}

	LOCK_UNSLEEPABLE_LOCK(&(fwlog_metabuffer.spin_lock));
	if (skb_queue_empty(&g_priv->adapter->fwlog_fops_queue)) {
		/* if (filp->f_flags & O_NONBLOCK) { */
		if (fwlog_metabuffer.write_p == fwlog_metabuffer.read_p) {
			UNLOCK_UNSLEEPABLE_LOCK(&(fwlog_metabuffer.spin_lock));
			return 0;
		}
	}

	do {
		skb = skb_dequeue(&g_priv->adapter->fwlog_fops_queue);
		if (skb == NULL) {
			pr_debug("%s skb=NULL break\n", __func__);
			break;
		}
#if 0
		pr_debug("%s pkt_type %d metabuffer.buffer %d",
			__func__, bt_cb(skb)->pkt_type,
			metabuffer.buffer[copyLen]);
#endif
		btmtk_print_buffer_conent(skb->data, skb->len);

		if (btmtk_sdio_push_data_to_metabuffer(&fwlog_metabuffer, skb->data,
				skb->len, bt_cb(skb)->pkt_type, false) < 0) {
			skb_queue_head(&g_priv->adapter->fwlog_fops_queue, skb);
			break;
		}
		kfree_skb(skb);
	} while (!skb_queue_empty(&g_priv->adapter->fwlog_fops_queue));
	UNLOCK_UNSLEEPABLE_LOCK(&(fwlog_metabuffer.spin_lock));

	return btmtk_sdio_pull_data_from_metabuffer(&fwlog_metabuffer, buf, count);
}

static ssize_t btmtk_fops_writefwlog(struct file *filp, const char __user *buf,
			size_t count, loff_t *f_pos)
{
	struct sk_buff *skb = NULL;
	int length = 0, i, j = 0;
	u8 *i_fwlog_buf = kmalloc(HCI_MAX_COMMAND_BUF_SIZE, GFP_KERNEL);
	u8 *o_fwlog_buf = kmalloc(HCI_MAX_COMMAND_SIZE, GFP_KERNEL);
	const char *val_param = NULL;
	unsigned long c_result = 0;
	u32 crAddr = 0, crValue = 0;

	memset(i_fwlog_buf, 0, HCI_MAX_COMMAND_BUF_SIZE);
	memset(o_fwlog_buf, 0, HCI_MAX_COMMAND_SIZE);
	memset(userbuf_fwlog, 0, MTK_TXDATA_SIZE);

	c_result = copy_from_user(userbuf_fwlog, buf, count);

	if (c_result != 0) {
		pr_err("%s copy_from_user failed!\n", __func__);
		return -EFAULT;
	}
	if (g_priv == NULL) {
		pr_info("%s g_priv is NULL\n", __func__);
		goto exit;
	}

	if (strstr(userbuf_fwlog, FW_OWN_OFF)) {
		pr_warn("%s set %s\n", __func__, FW_OWN_OFF);
		btmtk_sdio_set_no_fw_own(g_priv, true);
		wake_up_interruptible(&g_priv->main_thread.wait_q);
		goto exit;

	} else if (strstr(userbuf_fwlog, FW_OWN_ON)) {
		pr_warn("%s set %s\n", __func__, FW_OWN_ON);
		btmtk_sdio_set_no_fw_own(g_priv, false);
		wake_up_interruptible(&g_priv->main_thread.wait_q);
		goto exit;
	}

	if (count > HCI_MAX_COMMAND_BUF_SIZE) {
		pr_err("%s: your command is larger than maximum length, count = %zd\n",
			__func__, count);
		goto exit;
	}

	for (i = 0; i < count; i++) {
		if (userbuf_fwlog[i] != '=') {
			i_fwlog_buf[i] = userbuf_fwlog[i];
			pr_debug("%s: tag_param %02x\n",
				__func__, i_fwlog_buf[i]);
		} else {
			val_param = &userbuf_fwlog[i+1];
			if (strcmp(i_fwlog_buf, "log_lvl") == 0) {
				pr_info("%s: btmtk_log_lvl = %d\n",
					__func__, val_param[0] - 48);
#if 0
				btmtk_log_lvl = val_param[0] - 48;
#endif
			}
			goto exit;
		}
	}

	if (i == count) {
		/* hci input command format : */
		/* echo 01 be fc 01 05 > /dev/stpbtfwlog */
		/* We take the data from index three to end. */
		val_param = &userbuf_fwlog[0];
	}
	length = strlen(val_param);

	for (i = 0; i < length; i++) {
		u8 ret = 0;
		u8 temp_str[3] = { 0 };
		long res = 0;

		if (val_param[i] == ' ' || val_param[i] == '\t'
			|| val_param[i] == '\r' || val_param[i] == '\n')
			continue;
		if ((val_param[i] == '0' && val_param[i + 1] == 'x')
			|| (val_param[0] == '0' && val_param[i + 1] == 'X')) {
			i++;
			continue;
		}
		if (!(val_param[i] >= '0' && val_param[i] <= '9')
			&& !(val_param[i] >= 'A' && val_param[i] <= 'F')
			&& !(val_param[i] >= 'a' && val_param[i] <= 'f')) {
			break;
		}
		temp_str[0] = *(val_param+i);
		temp_str[1] = *(val_param+i+1);
		ret = (u8) (kstrtol((char *)temp_str, 16, &res) & 0xff);
		o_fwlog_buf[j++] = res;
		if (j >= (HCI_MAX_COMMAND_SIZE - 1) && ((i+1) < length)) {
			pr_warn("%s buf is full, input format may error\n", __func__);
			goto exit;
		}

		i++;
	}
	length = j;

	/*
	 * Receive command from stpbtfwlog, then Sent hci command
	 * to controller
	 */
	pr_debug("%s: hci buff is %02x%02x%02x%02x%02x, length %d\n",
		__func__, o_fwlog_buf[0], o_fwlog_buf[1],
		o_fwlog_buf[2], o_fwlog_buf[3], o_fwlog_buf[4], length);
	/* check HCI command length */
	if (length > HCI_MAX_COMMAND_SIZE) {
		pr_err("%s: your command is larger than maximum length, length = %d\n",
			__func__, length);
		goto exit;
	}

	if (length <= 1) {
		pr_err("%s: length = %d, command format may error\n", __func__, length);
		goto exit;
	}

	pr_debug("%s: hci buff is %02x%02x%02x%02x%02x\n",
		__func__, o_fwlog_buf[0], o_fwlog_buf[1],
		o_fwlog_buf[2], o_fwlog_buf[3], o_fwlog_buf[4]);

	switch (o_fwlog_buf[0]) {
	case MTK_HCI_READ_CR_PKT:
		if (length == MTK_HCI_READ_CR_PKT_LENGTH) {
			crAddr = (o_fwlog_buf[1] << 24) + (o_fwlog_buf[2] << 16) +
			(o_fwlog_buf[3] << 8) + (o_fwlog_buf[4]);
			btmtk_sdio_readl(crAddr, &crValue);
			pr_info("%s read crAddr=0x%08x crValue=0x%08x\n",
				__func__, crAddr, crValue);
		} else
			pr_info("%s read length=%d is incorrect, should be %d\n",
				__func__, length, MTK_HCI_READ_CR_PKT_LENGTH);
		break;

	case MTK_HCI_WRITE_CR_PKT:
		if (length == MTK_HCI_WRITE_CR_PKT_LENGTH) {
			crAddr = (o_fwlog_buf[1] << 24) + (o_fwlog_buf[2] << 16) +
			(o_fwlog_buf[3] << 8) + (o_fwlog_buf[4]);
			crValue = (o_fwlog_buf[5] << 24) + (o_fwlog_buf[6] << 16) +
			(o_fwlog_buf[7] << 8) + (o_fwlog_buf[8]);
			pr_info("%s write crAddr=0x%08x crValue=0x%08x\n",
				__func__, crAddr, crValue);
			btmtk_sdio_writel(crAddr, crValue);
		} else
			pr_info("%s write length=%d is incorrect, should be %d\n",
				__func__, length, MTK_HCI_WRITE_CR_PKT_LENGTH);


		break;

	default:
		/*
		 * Receive command from stpbtfwlog, then Sent hci command
		 * to Stack
		 */
		skb = bt_skb_alloc(length - 1, GFP_ATOMIC);
		bt_cb(skb)->pkt_type = o_fwlog_buf[0];
		memcpy(&skb->data[0], &o_fwlog_buf[1], length - 1);
		skb->len = length - 1;
		skb_queue_tail(&g_priv->adapter->tx_queue, skb);
		wake_up_interruptible(&g_priv->main_thread.wait_q);
		break;
	}



	pr_info("%s write end\n", __func__);
exit:
	pr_info("%s exit, length = %d\n", __func__, length);
	kfree(i_fwlog_buf);
	kfree(o_fwlog_buf);
	return count;
}

static unsigned int btmtk_fops_pollfwlog(
			struct file *file, poll_table *wait)
{
	unsigned int mask = 0;

	if (!probe_ready) {
		pr_err("%s probe_ready is %d return\n",
			__func__, probe_ready);
		return mask;
	}

	if (g_priv == NULL) {
		pr_err("%s g_priv is NULL\n", __func__);
		return -ENODEV;
	}

	if (fwlog_metabuffer.write_p != fwlog_metabuffer.read_p)
		mask |= (POLLIN | POLLRDNORM);

	if (skb_queue_empty(&g_priv->adapter->fwlog_fops_queue)) {
		poll_wait(file, &fw_log_inq, wait);

		if (!skb_queue_empty(&g_priv->adapter->fwlog_fops_queue)) {
			mask |= (POLLIN | POLLRDNORM);
			/* pr_info("%s poll done\n", __func__); */
		}
	} else
		mask |= (POLLIN | POLLRDNORM);

	mask |= (POLLOUT | POLLWRNORM);

	/* pr_info("%s poll mask 0x%x\n", __func__,mask); */
	return mask;
}

static long btmtk_fops_unlocked_ioctlfwlog(
			struct file *filp, unsigned int cmd,
			unsigned long arg)
{
	int retval = 0;

	pr_info("%s: ->\n", __func__);
	if (g_priv == NULL) {
		pr_err("%s: ERROR, g_data is NULL!\n", __func__);
		return -ENODEV;
	}

	return retval;
}

/*============================================================================*/
/* Interface Functions : Proc */
/*============================================================================*/
#define __________________________________________Interface_Function_for_Proc
static int btmtk_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", fw_version_str);
	return 0;
}

static int btmtk_proc_open(struct inode *inode, struct  file *file)
{
	return single_open(file, btmtk_proc_show, NULL);
}

void btmtk_proc_create_new_entry(void)
{
	struct proc_dir_entry *proc_show_entry;

	pr_info("proc initialized");

	g_proc_dir = proc_mkdir("stpbt", 0);
	if (g_proc_dir == 0) {
		pr_info("Unable to creat dir");
		return;
	}
	proc_show_entry =  proc_create("bt_fw_version", 0644, g_proc_dir, &BT_proc_fops);
}


static int BTMTK_major;
static int BT_majorfwlog;
static struct cdev BTMTK_cdev;
static struct cdev BT_cdevfwlog;
static int BTMTK_devs = 1;

static wait_queue_head_t inq;
const struct file_operations BTMTK_fops = {
	.open = btmtk_fops_open,
	.release = btmtk_fops_close,
	.read = btmtk_fops_read,
	.write = btmtk_fops_write,
	.poll = btmtk_fops_poll,
	.unlocked_ioctl = btmtk_fops_unlocked_ioctl,
	.fasync = btmtk_fops_fasync
};

const struct file_operations BT_fopsfwlog = {
	.open = btmtk_fops_openfwlog,
	.release = btmtk_fops_closefwlog,
	.read = btmtk_fops_readfwlog,
	.write = btmtk_fops_writefwlog,
	.poll = btmtk_fops_pollfwlog,
	.unlocked_ioctl = btmtk_fops_unlocked_ioctlfwlog

};

static int BTMTK_init(void)
{
	dev_t devID = MKDEV(BTMTK_major, 0);
	dev_t devIDfwlog = MKDEV(BT_majorfwlog, 1);
	int ret = 0;
	int cdevErr = 0;
	int major = 0;
	int majorfwlog = 0;

	pr_info("BTMTK_init\n");

	g_card = NULL;
	txbuf = NULL;
	rxbuf = NULL;
	userbuf = NULL;
	rx_length = 0;

#if SAVE_FW_DUMP_IN_KERNEL
	fw_dump_file = NULL;
#else
	fw_dump_file = 0;
#endif
	g_priv = NULL;



	probe_counter = 0;
	fw_is_doing_coredump = 0;

	btmtk_proc_create_new_entry();

	ret = alloc_chrdev_region(&devID, 0, 1, "BT_chrdev");
	if (ret) {
		pr_err("fail to allocate chrdev\n");
		return ret;
	}

	ret = alloc_chrdev_region(&devIDfwlog, 0, 1, "BT_chrdevfwlog");
	if (ret) {
		pr_err("fail to allocate chrdev\n");
		return ret;
	}

	BTMTK_major = major = MAJOR(devID);
	pr_info("major number:%d\n", BTMTK_major);
	BT_majorfwlog = majorfwlog = MAJOR(devIDfwlog);
	pr_info("BT_majorfwlog number: %d\n", BT_majorfwlog);

	cdev_init(&BTMTK_cdev, &BTMTK_fops);
	BTMTK_cdev.owner = THIS_MODULE;

	cdev_init(&BT_cdevfwlog, &BT_fopsfwlog);
	BT_cdevfwlog.owner = THIS_MODULE;

	cdevErr = cdev_add(&BTMTK_cdev, devID, BTMTK_devs);
	if (cdevErr)
		goto error;

	cdevErr = cdev_add(&BT_cdevfwlog, devIDfwlog, 1);
	if (cdevErr)
		goto error;

	pr_info("%s driver(major %d) installed.\n",
			"BT_chrdev", BTMTK_major);
	pr_info("%s driver(major %d) installed.\n",
			"BT_chrdevfwlog", BT_majorfwlog);

	pBTClass = class_create(THIS_MODULE, "BT_chrdev");
	if (IS_ERR(pBTClass)) {
		pr_err("class create fail, error code(%ld)\n",
			PTR_ERR(pBTClass));
		goto err1;
	}

	pBTDev = device_create(pBTClass, NULL, devID, NULL, BT_NODE);
	if (IS_ERR(pBTDev)) {
		pr_err("device create fail, error code(%ld)\n",
			PTR_ERR(pBTDev));
		goto err2;
	}

	pBTDevfwlog = device_create(pBTClass, NULL,
				devIDfwlog, NULL, "stpbtfwlog");
	if (IS_ERR(pBTDevfwlog)) {
		pr_err("device(stpbtfwlog) create fail, error code(%ld)\n",
			PTR_ERR(pBTDevfwlog));
		goto err2;
	}

	pr_info("%s: BT_major %d, BT_majorfwlog %d\n",
			__func__, BTMTK_major, BT_majorfwlog);
	pr_info("%s: devID %d, devIDfwlog %d\n", __func__, devID, devIDfwlog);

	/* init wait queue */
	g_devIDfwlog = devIDfwlog;
	init_waitqueue_head(&(fw_log_inq));
	init_waitqueue_head(&(inq));

	return 0;

 err2:
	if (pBTClass) {
		class_destroy(pBTClass);
		pBTClass = NULL;
	}

 err1:

 error:
	if (cdevErr == 0)
		cdev_del(&BTMTK_cdev);

	if (ret == 0)
		unregister_chrdev_region(devID, BTMTK_devs);

	return -1;
}

static void BTMTK_exit(void)
{
	dev_t dev = MKDEV(BTMTK_major, 0);
	dev_t devIDfwlog = g_devIDfwlog;

	pr_info("%s\n", __func__);

	if (g_proc_dir != NULL) {
		remove_proc_entry("bt_fw_version", g_proc_dir);
		remove_proc_entry("stpbt", NULL);
		g_proc_dir = NULL;
		pr_info("proc device node and folder removed!!");
	}

	if (g_card) {
		pr_info("%s 1\n", __func__);
		if (g_card->woble_setting_file_name) {
			pr_info("%s 2\n", __func__);
			kfree(g_card->woble_setting_file_name);
			pr_info("%s 3\n", __func__);
			pr_info("free woble_setting_file_name done\n");
		} else {
			pr_err("%s no free woble_setting_file_name, g_card is not null\n", __func__);
			pr_err("%s woble_setting_file_name is null\n", __func__);
		}
	} else
		pr_err("no free woble_setting_file_name, g_card is null\n");

#if (SUPPORT_UNIFY_WOBLE & SUPPORT_ANDROID)
		pr_info("%s 4\n", __func__);
		if (g_card)
			wake_lock_destroy(&g_card->woble_wlock);
		else
			pr_err("%s:g_data is NULL, no destroy woble_wlock", __func__);
#endif

	pr_info("%s 5\n", __func__);
	if (pBTDevfwlog) {
		pr_info("%s 6\n", __func__);
		device_destroy(pBTClass, devIDfwlog);
		pBTDevfwlog = NULL;
	}
	pr_info("%s 7\n", __func__);
	if (pBTDev) {
		device_destroy(pBTClass, dev);
		pBTDev = NULL;
	}
	pr_info("%s 8\n", __func__);
	if (pBTClass) {
		class_destroy(pBTClass);
		pBTClass = NULL;
	}
	pr_info("%s 9\n", __func__);
	cdev_del(&BTMTK_cdev);
	pr_info("%s 10\n", __func__);
	unregister_chrdev_region(dev, 1);

	cdev_del(&BT_cdevfwlog);
	unregister_chrdev_region(devIDfwlog, 1);
	pr_info("%s driver removed.\n", BT_DRIVER_NAME);
}

static int btmtk_sdio_allocate_memory(void)
{
	if (txbuf == NULL) {
		txbuf = kmalloc(MTK_TXDATA_SIZE, GFP_ATOMIC);
		memset(txbuf, 0, MTK_TXDATA_SIZE);
	}

	if (rxbuf == NULL) {
		rxbuf = kmalloc(MTK_RXDATA_SIZE, GFP_ATOMIC);
		memset(rxbuf, 0, MTK_RXDATA_SIZE);
	}

	if (userbuf == NULL) {
		userbuf = kmalloc(MTK_TXDATA_SIZE, GFP_ATOMIC);
		memset(userbuf, 0, MTK_TXDATA_SIZE);
	}

	if (userbuf_fwlog == NULL) {
		userbuf_fwlog = kmalloc(MTK_TXDATA_SIZE, GFP_ATOMIC);
		memset(userbuf_fwlog, 0, MTK_TXDATA_SIZE);
	}

	return 0;
}

static int btmtk_sdio_free_memory(void)
{
	kfree(txbuf);

	kfree(rxbuf);

	kfree(userbuf);

	kfree(userbuf_fwlog);

	return 0;
}

static int __init btmtk_sdio_init_module(void)
{
	int ret = 0;

	ret = BTMTK_init();
	if (ret) {
		pr_err("%s: BTMTK_init failed!", __func__);
		return ret;
	}

	if (btmtk_sdio_allocate_memory() < 0) {
		pr_err("%s: allocate memory failed!", __func__);
		return -ENOMEM;
	}

	if (sdio_register_driver(&bt_mtk_sdio) != 0) {
		pr_err("SDIO Driver Registration Failed\n");
		return -ENODEV;
	}

	pr_info("SDIO Driver Registration Success\n");

	/* Clear the flag in case user removes the card. */
	user_rmmod = 0;

	return 0;
}

static void __exit btmtk_sdio_exit_module(void)
{
	/* Set the flag as user is removing this module. */
	user_rmmod = 1;
	BTMTK_exit();
	sdio_unregister_driver(&bt_mtk_sdio);
	btmtk_sdio_free_memory();
}

module_init(btmtk_sdio_init_module);
module_exit(btmtk_sdio_exit_module);
