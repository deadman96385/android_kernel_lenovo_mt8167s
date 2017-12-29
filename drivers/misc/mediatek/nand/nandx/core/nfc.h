/*
 * Copyright (C) 2017 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 */

#ifndef __NFC_H__
#define __NFC_H__

#include "nfc_core.h"

extern void nfc_send_command(struct nfc_handler *handler, u8 cmd);
extern void nfc_send_address(struct nfc_handler *handler, u32 col, u32 row,
			     u32 col_cycle, u32 row_cycle);
extern u8 nfc_read_byte(struct nfc_handler *handler);
extern void nfc_write_byte(struct nfc_handler *handler, u8 data);
extern int nfc_read_sectors(struct nfc_handler *handler, int num, u8 *data,
			    u8 *fdm);
extern int nfc_write_page(struct nfc_handler *handler, u8 *data, u8 *fdm);
extern int nfc_change_interface(struct nfc_handler *handler,
				 enum INTERFACE_TYPE type,
				 struct nand_timing *timing, void *arg);
extern int nfc_change_mode(struct nfc_handler *handler, enum OPS_MODE_TYPE mode,
			   bool enable, void *arg);
extern bool nfc_get_mode(struct nfc_handler *handler, enum OPS_MODE_TYPE mode);
extern void nfc_select_chip(struct nfc_handler *handler, int cs);
extern void nfc_set_format(struct nfc_handler *handler,
			   struct nfc_format *format);
extern void nfc_enable_randomizer(struct nfc_handler *handler, u32 page,
				  bool encode);
extern void nfc_disable_randomizer(struct nfc_handler *handler);
extern int nfc_wait_busy(struct nfc_handler *handler, int timeout,
			    enum WAIT_TYPE type);
extern int nfc_calculate_ecc(struct nfc_handler *handler, u8 *data, u8 *ecc,
			     u32 len, u8 ecc_strength);
extern int nfc_correct_ecc(struct nfc_handler *handler, u8 *data, u32 len,
			   u8 ecc_strength);
extern int nfc_calibration(struct nfc_handler *handler);
extern struct nfc_handler *nfc_setup_hw(struct nfc_resource *res);
extern void nfc_release(struct nfc_handler *handler);
extern int nfc_suspend(struct nfc_handler *handler);
extern int nfc_resume(struct nfc_handler *handler);

#endif
