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

#ifndef __NANDX_BMT_H__
#define __NANDX_BMT_H__

#include "nandx_util.h"
#include "nandx_info.h"
#ifdef CONFIG_MNTL_SUPPORT
#include "mntl_ops.h"
#endif


enum UPDATE_REASON {
	UPDATE_ERASE_FAIL,
	UPDATE_WRITE_FAIL,
	UPDATE_UNMAPPED_BLOCK,
	UPDATE_REMOVE_ENTRY,
	UPDATE_INIT_WRITE,
	UPDATE_REASON_COUNT,
	INIT_BAD_BLK,
	FTL_MARK_BAD = 64,
};


int nandx_bmt_init(struct nandx_chip_info *dev_info, u32 block_num);
u32 nandx_bmt_update(u32 bad_block, enum UPDATE_REASON reason);
void nandx_bmt_update_oob(u32 block, u8 *oob);
u32 nandx_bmt_get_mapped_block(u32 block);

#ifdef CONFIG_MNTL_SUPPORT
int nandx_bmt_get_data_bmt(struct data_bmt_struct *data_bmt);

#endif


#endif /* #ifndef __NANDX_BMT_H__ */
