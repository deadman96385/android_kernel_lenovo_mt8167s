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

#ifndef __MTD_OPS_H__
#define __MTD_OPS_H__

#include <linux/mtd/mtd.h>

int nand_write(struct mtd_info *mtd, long long to, size_t len,
			  size_t *retlen, const uint8_t *buf);
int nand_read(struct mtd_info *mtd, long long from, size_t len,
		     size_t *retlen, uint8_t *buf);
int nand_erase(struct mtd_info *mtd, struct erase_info *instr);
int nand_read_oob(struct mtd_info *mtd, long long from,
		      struct mtd_oob_ops *ops);
int nand_write_oob(struct mtd_info *mtd, long long to,
		      struct mtd_oob_ops *ops);
int nand_is_bad(struct mtd_info *mtd, long long ofs);
int nand_mark_bad(struct mtd_info *mtd, long long ofs);
void nand_sync(struct mtd_info *mtd);

#endif /* __MTD_OPS_H__ */
