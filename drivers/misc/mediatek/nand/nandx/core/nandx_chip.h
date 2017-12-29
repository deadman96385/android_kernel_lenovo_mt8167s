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


#ifndef __NANDX_CHIP_H__
#define __NANDX_CHIP_H__

#include "nandx_util.h"
#include "nandx_info.h"
#include "nfc_core.h"

enum PROGRAM_ORDER {
	PROGRAM_ORDER_NONE,
	PROGRAM_ORDER_TLC,
	PROGRAM_ORDER_VTLC_TOSHIBA,
	PROGRAM_ORDER_VTLC_MICRON,
};

struct nandx_chip_dev {
	struct nandx_chip_info info;
	u8 program_order_type;
	int (*read_page)(struct nandx_chip_dev *, struct nandx_ops *);
	int (*cache_read_page)(struct nandx_chip_dev *, struct nandx_ops **,
			       int);
	int (*multi_read_page)(struct nandx_chip_dev *, struct nandx_ops **,
			       int);
	int (*multi_cache_read_page)(struct nandx_chip_dev *,
				     struct nandx_ops **, int);
	int (*program_page)(struct nandx_chip_dev *, struct nandx_ops **,
			    int);
	int (*cache_program_page)(struct nandx_chip_dev *, struct nandx_ops **,
				  int);
	int (*multi_program_page)(struct nandx_chip_dev *, struct nandx_ops **,
				  int);
	int (*multi_cache_program_page)(struct nandx_chip_dev *,
					struct nandx_ops **, int);
	int (*erase)(struct nandx_chip_dev *, u32);
	int (*multi_erase)(struct nandx_chip_dev *, u32 *);
	int (*multi_plane_check)(struct nandx_chip_dev *, u32 *);
	bool (*block_is_bad)(struct nandx_chip_dev *, u32);
	int (*change_mode)(struct nandx_chip_dev *, enum OPS_MODE_TYPE, bool,
			   void *);
	bool (*get_mode)(struct nandx_chip_dev *, enum OPS_MODE_TYPE);
	int (*suspend)(struct nandx_chip_dev *);
	int (*resume)(struct nandx_chip_dev *);
};


struct nandx_chip_dev *nandx_chip_alloc(struct nfc_resource *res);
void nandx_chip_free(struct nandx_chip_dev *chip_dev);

#endif /* __NANDX_CHIP_H__ */
