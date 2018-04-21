/*
 * Copyright (C) 2017 MediaTek Inc.
 * Licensed under either
 *     BSD Licence, (see NOTICE for more details)
 *     GNU General Public License, version 2.0, (see NOTICE for more details)
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

#endif				/* #ifndef __NANDX_BMT_H__ */
