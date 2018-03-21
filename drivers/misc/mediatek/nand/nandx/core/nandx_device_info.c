/*
 * Copyright (C) 2017 MediaTek Inc.
 * Licensed under either
 *     BSD Licence, (see NOTICE for more details)
 *     GNU General Public License, version 2.0, (see NOTICE for more details)
 */

#include "nandx_device_info.h"
#include "nandx_chip.h"

/*
 * Notice:
 * 1. Please always add item at first line
 * 2. Must comment the modified_date:yours_name when you add
 *    a new Nand information
 * 3. Please check the max item numbers:TABLE_MAX_NUM
 */
static struct nandx_device_info nandx_device_table[] = {
	/*
	 * name, id, type,
	 * io_width, addr_cycle, target_num, lun_num, plane_num,
	 * block_num per plane, block_size, page_size, spare_size
	 * mode_type, program_order_type, address_table_type,
	 * bad_block_type, read_retry_type, interface_type,
	 * sdr_timing_type, ddr_timing_type,
	 * slc_life, xlc_life(pe_cycle; ecc_required; bitflips_threshold;)
	 */

	/* Number: 3, Author: xx, Date: 2017/11/14 */
	{"MT29F64G08CBCGB", {0x2c, 0x64, 0x44, 0x32, 0xa5, 0x00}, NAND_VMLC,
	 IO_8BIT, ADDR_CYCLE5, 1, 1, 2,
	 544, MB(8), KB(16), 2144,
	 MODE_SLC_DA, PROGRAM_ORDER_NONE, ADDRESSING_K,
	 BAD_BLOCK_READ_OOB, RR_MLC_MICRON, INTERFACE_ONFI,
	 SDR_TIMING_MICRON_VMLC, ONFI_TIMING_MICRON_VMLC,
	 {2000, 72, 46}, {25000, 72, 46} },

	/* Number: 2, Author: xx, Date: 2017/9/27 */
	{"SDTNSIAMA016G", {0x45, 0x4c, 0x98, 0xa3, 0x76, 0x00}, NAND_TLC,
	 IO_8BIT, ADDR_CYCLE5, 1, 1, 2,
	 1446, MB(6), KB(16), 1952,
	 MODE_SLC_A2, PROGRAM_ORDER_TLC, ADDRESSING_B,
	 BAD_BLOCK_SLC_PROGRAM, RR_TLC_SANDISK_1ZNM, INTERFACE_TOGGLE,
	 SDR_TIMING_SANDISK_TLC, TOGGLE_TIMING_SANDISK_TLC,
	 {500, 68, 46}, {50000, 68, 46} },

	/* Number: 1, Author: xx, Date: 2017/9/26 */
	{"TC58TEG7THLBA09", {0x98, 0x3a, 0x98, 0xa3, 0x76, 0x51}, NAND_TLC,
	 IO_8BIT, ADDR_CYCLE5, 1, 1, 2,
	 1446, MB(6), KB(16), 1952,
	 MODE_SLC_A2, PROGRAM_ORDER_TLC, ADDRESSING_A,
	 BAD_BLOCK_READ_OOB, RR_TLC_TOSHIBA_15NM, INTERFACE_LEGACY,
	 SDR_TIMING_TOSHIBA_TLC, TOGGLE_TIMING_TOSHIBA_TLC,
	 {500, 68, 46}, {50000, 68, 46} },

	/* NONE , MUST BE THE LAST ONE */
	{"NO-DEVICE", {0, 0, 0, 0, 0, 0,}, 0,
	 0, 0, 0, 0, 0,
	 0, 0, 0, 0,
	 0, 0, 0,
	 0, 0, 0,
	 0, 0,
	 {0, 0, 0}, {0, 0, 0} },
};

static struct nandx_legacy_timing
nandx_legacy_timing_table[SDR_TIMING_TYPE_NUM] = {
	/*
	 * tREA; tREH; tCR; tRP; tWP; tWH;
	 * tWHR; tCLS; tALS; tCLH; tALH; tWC; tRC;
	 */
	{16, 7, 10, 10, 10, 7,
	 120, 10, 10, 5, 5, 20, 20},	/* LEGACY_TIMING_SANDISK_TLC */

	{16, 7, 9, 10, 10, 7,
	 120, 10, 10, 5, 5, 20, 20},	/* LEGACY_TIMING_TOSHIBA_TLC */

	{16, 7, 10, 10, 10, 7,
	 60, 10, 10, 5, 5, 20, 20},	/* LEGACY_TIMING_MICRON_VMLC_133MHZ */
};

static struct nandx_onfi_timing
nandx_onfi_timing_table[ONFI_TIMING_TYPE_NUM] = {
	/* tCAD; tWPRE; tWPST; tWRCK; tCKWR; tWHR; */
	{25, 22, 22, 20, 45, 80},	/* ONFI_TIMING_MICRON_VMLC */
};

static struct nandx_toggle_timing
nandx_toggle_timing_table[TOGGLE_TIMING_TYPE_NUM] = {
	/*
	 * tCS; tCH; tCAS; tCAH; tCALS; tCALH; tWP; tWPRE; tWPST; tWPSTH;
	 * tCR; tRPRE; tRPST; tRPSTH; tCDQSS; tWHR;
	 */
	{20, 5, 5, 5, 15, 5, 11, 15, 7, 25,
	 10, 15, 30, 25, 100, 120},	/* TOGGLE_TIMING_SANDISK_TLC */

	{20, 5, 5, 5, 15, 5, 11, 15, 7, 25,
	 10, 15, 30, 25, 100, 120},	/* TOGGLE_TIMING_TOSHIBA_TLC */
};

static u8 nandx_feature_table[VENDOR_NUM][FEATURE_ADDRESS_NUM] = {
	/*
	 * ODT, DRIVE_STRENGTH, EXTERNAL_VPP, INTERFACE_CHANGE, DQS_LATENCY,
	 * SLEW_RATE,TIMING_MODE,RB_STRENGTH, NV_DDR,FEATURE_READ_RETRY
	 */
	/* VENDOR_TOSHIBA */
	{0x02, 0x10, 0x30, 0x80, NONE, NONE, NONE, NONE, NONE, NONE},
	/* VENDOR_SANDISK */
	{0x02, 0x10, NONE, 0x80, 0x02, 0x83, NONE, NONE, NONE, 0x11},
	/* VENDOR_HYNIX */
	{0x02, 0x10, 0x30, 0x01, NONE, 0x83, NONE, NONE, NONE, NONE},
	/* VENDOR_MICRON */
	{0x02, 0x10, 0x30, 0x01, NONE, NONE, 0x01, 0x81, 0x02, 0x89},
};

static u8 nandx_interface_value_table[VENDOR_NUM][8] = {
	/* bit7 ~ bit 0 */
	/* VENDOR_TOSHIBA */
	{NONE, NONE, NONE, NONE, NONE, NONE, NONE, INFTYPE},
	/* VENDOR_SANDISK */
	{NONE, NONE, NONE, NONE, NONE, NONE, NONE, INFTYPE},
	/* VENDOR_HYNIX */
	{NONE, NONE, INFTYPE, INFTYPE, NONE, NONE, NONE, NONE},
	/* VENDOR_MICRON */
	{NONE, NONE, INFTYPE, INFTYPE, TMODE, TMODE, TMODE, TMODE},
};

/* how to choose */
static u8 nandx_addressing_table[ADDRESSING_TABLE_NUM][ADDR_INDEX_NUM] = {
	/*
	 * ADDR_ROW_START, ADDR_ROW_LEN, ADDR_LOGICAL_PLANE_START,
	 * ADDR_LOGICAL_PLANE_LEN, ADDR_PLANE_START, ADDR_PLANE_LEN,
	 * ADDR_BLOCK_START, ADDR_BLOCK_LEN, ADDR_LUN_START, ADDR_LUN_LEN
	 */
	{16, 7, NONE, 0, 24, 1, 25, 11, 36, 1},	/* ADDRESSING_A */
	{16, 7, NONE, 0, 24, 1, 25, 11, 36, 3},	/* ADDRESSING_B */
	{16, 7, 24, 1, NONE, 0, 25, 12, 37, 3},	/* ADDRESSING_C */
	{16, 7, 24, 1, 25, 1, 26, 12, 38, 2},	/* ADDRESSING_D */
	{16, 8, NONE, 0, 24, 1, 24, 12, NONE, 0},	/* ADDRESSING_E */
	{16, 8, NONE, 0, 24, 1, 24, 12, 36, 1},	/* ADDRESSING_F */
	{16, 8, NONE, 0, 24, 1, 24, 12, 36, 2},	/* ADDRESSING_G */
	{16, 8, NONE, 0, 24, 1, 25, 11, 36, 2},	/* ADDRESSING_H */
	{16, 8, NONE, 0, 24, 1, 25, 11, 36, 3},	/* ADDRESSING_I */
	{16, 8, NONE, 0, 24, 1, 25, 12, 37, 3},	/* ADDRESSING_J */
	{16, 9, NONE, 0, 25, 1, 25, 11, 36, 1},	/* ADDRESSING_K */
	{16, 9, NONE, 0, 25, 1, 25, 12, 37, 1},	/* ADDRESSING_L */
	{16, 10, NONE, 0, 26, 1, 26, 12, 38, 1},	/* ADDRESSING_M */
	{16, 12, NONE, 0, 28, 1, 28, 10, 38, 1}	/* ADDRESSING_N */
};

static u8 nandx_vendor_table[VENDOR_NUM] = {
	/* SAMSUNG,TOSHIBA,SANDISK,HYNIX,MICRON */
	0x98, 0x45, 0xad, 0x2c, 0xec,
};

static u8 nandx_drive_strength[DRIVE_STRENGTH_TYPE_NUM][DRIVE_LEVEL_NUM] = {
	/*
	 * DRIVE_LEVEL_DEFAULT, DRIVE_LEVEL_LOW, DRIVE_LEVEL_HIGH,
	 * DRIVE_LEVEL_MORE
	 */
	{0x02, 0x01, 0x03, 0x00},	/* DRIVE_STRENGTH_MICRON */
	{0x04, 0x02, 0x06, NONE},	/* DRIVE_STRENGTH_TOSHIBA */
	{0x04, 0x02, 0x06, 0x08}	/* DRIVE_STRENGTH_HYNIX */
};

static u8 basic_cmd_sets[CMD_NUM] = {
	0xff,			/* CMD_RESET */
	0xfa,			/* CMD_RESET_LUN */
	0xfc,			/* CMD_RESET_SYNC */
	0x90,			/* CMD_READ_ID */
	0x70,			/* CMD_READ_STATUS */
	0x79,			/* CMD_READ_ENHANCE_STATUS */
	0xec,			/* CMD_READ_PARAMETERS_PAGE */
	0xef,			/* CMD_SET_FEATURE */
	0xee,			/* CMD_GET_FEATURE */
	0xd5,			/* CMD_SET_LUN_FEATURE */
	0xd4,			/* CMD_GET_LUN_FEATURE */
	0x00,			/* CMD_READ_1ST */
	0x30,			/* CMD_READ_2ND */
	0x31,			/* CMD_CACHE_READ_2ND */
	0x3f,			/* CMD_CACHE_READ_LAST */
	0x32,			/* CMD_MULTI_READ_2ND */
	0x05,			/* CMD_RANDOM_OUTPUT0 */
	0xe0,			/* CMD_RANDOM_OUTPUT1 */
	0x80,			/* CMD_PROGRAM_1ST */
	0x10,			/* CMD_PROGRAM_2ND */
	0x15,			/* CMD_CACHE_PROGRAM_2ND */
	0x11,			/* CMD_MULTI_PROGRAM_2ND */
	0x80,			/* CMD_MULTI2_PROGRAM_1ST */
	0x10,			/* CMD_MULTI2_PROGRAM_2ND */
	0x60,			/* CMD_BLOCK_ERASE0 */
	0xd0			/* CMD_BLOCK_ERASE1 */
};

static u8 extend_cmd_sets[EXTEND_CMD_TYPE_NUM][EXTEND_CMD_SETS_NUM + 1] = {
	/* cmd_sets_num, cmd0, cmd1, ... */
	{3, 0x01, 0x02, 0x03},	/* EXTEND_TLC_PRE_CMD */
	{2, 0x09, 0x0d, NONE}	/* EXTEND_TLC_PROGRAM_ORDER_CMD */
};

static u8 replace_cmd_sets[REPLACE_CMD_TYPE_NUM][2] = {
	/* BASIC_CMD_SETS type, new cmd value */
	{CMD_PROGRAM_2ND, 0x1a},	/* RCMD_PROGRAM_2ND */
	{CMD_CACHE_PROGRAM_2ND, 0x1a},	/* RCMD_CACHE_PROGRAM_2ND */
	{CMD_MULTI2_PROGRAM_1ST, 0x85}	/* RCMD_MULTI2_PROGRAM_1ST */
};

u8 *get_basic_cmd_sets(void)
{
	return basic_cmd_sets;
}

u8 get_extend_cmd(enum EXTEND_CMD_TYPE type, int cycle)
{
	if (type >= EXTEND_CMD_TYPE_NUM || type < 0)
		return NONE;

	if (cycle >= extend_cmd_sets[type][0] || cycle < 0)
		return NONE;

	return extend_cmd_sets[type][cycle];
}

void set_replace_cmd(enum REPLACE_CMD_TYPE type)
{
	int index;

	if (type >= REPLACE_CMD_TYPE_NUM || type < 0)
		return;

	index = replace_cmd_sets[type][0];
	basic_cmd_sets[index] = replace_cmd_sets[type][1];
}

u32 get_physical_row_address(u8 *addr, u32 lun, u32 plane, u32 block, u32 wl)
{
	u32 shift;

	if (addr[ADDR_LOGICAL_PLANE_START] == NONE)
		shift = addr[ADDR_PLANE_START] - addr[ADDR_ROW_START];
	else
		shift = addr[ADDR_LOGICAL_PLANE_START] - addr[ADDR_ROW_START];

	plane = plane << shift;
	if (addr[ADDR_BLOCK_START] != addr[ADDR_PLANE_START])
		block >>= addr[ADDR_PLANE_LEN];
	block = block << (addr[ADDR_BLOCK_START] - addr[ADDR_ROW_START]);

	if (addr[ADDR_LUN_START] != NONE)
		lun = lun << (addr[ADDR_LUN_START] - addr[ADDR_ROW_START]);

	return (lun | block | plane | wl);
}

struct nandx_device_info *get_nandx_device_info(u8 *id, int num)
{
	int i, j, id_num;
	int same_count = 0;
	struct nandx_device_info *dev_info;

	id_num = MIN(num, ID_MAX_NUM);

	for (i = 0; i < TABLE_MAX_NUM; i++) {
		if (!strcmp(nandx_device_table[i].name, "NO-DEVICE"))
			break;

		for (j = 0; j < id_num; j++) {
			if (id[j] != nandx_device_table[i].id[j])
				break;
		}
		if (j == id_num) {
			same_count++;
			dev_info = &nandx_device_table[i];
		}
	}

	if (dev_info)
		pr_info("[NAND]: %s\n", dev_info->name);

	NANDX_ASSERT(same_count == 1);

	return (same_count == 1 ? dev_info : NULL);
}

u8 *get_nandx_feature_table(enum VENDOR_TYPE vtype)
{
	return &nandx_feature_table[vtype][0];
}

u8 get_nandx_interface_value(enum VENDOR_TYPE vtype, u8 timing_mode,
			     enum INTERFACE_TYPE itype)
{
	u32 i, timing_start = 0, timing_len = 0;
	u32 interface_start = 0, interface_len = 0;
	u8 feature = 0;
	u8 *interface_value = &nandx_interface_value_table[vtype][0];
	u8 mapping_table[VENDOR_NUM][INTERFACE_NUM] = {
		/* INTERFACE_LEGACY, INTERFACE_ONFI, INTERFACE_TOGGLE */
		{1, NONE, 0},	/* VENDOR_TOSHIBA */
		{1, NONE, 0},	/* VENDOR_SANDISK */
		{0, 1, NONE},	/* VENDOR_HYNIX */
		{0, 1, NONE},	/* VENDOR_MICRON */
	};
	u8 interface_mapping = mapping_table[vtype][itype];

	for (i = 0; i < 8; i++) {
		if (interface_value[i] == INFTYPE) {
			interface_len++;
			interface_start = 7 - i;
		} else if (interface_value[i] == TMODE) {
			timing_len++;
			timing_start = 7 - i;
		}
	}

	if (interface_len)
		feature |= interface_mapping << interface_start;
	if (timing_len)
		feature |= timing_mode << timing_start;
	pr_debug("%s: change interface value 0x%x\n", __func__, feature);

	return feature;
}

u8 *get_nandx_addressing_table(enum ADDRESS_TABLE type)
{
	return &nandx_addressing_table[type][0];
}

u8 *get_nandx_drive_strength_table(enum DRIVE_STRENGTH_TYPE type)
{
	return &nandx_drive_strength[type][0];
}

u8 get_vendor_type(u8 id)
{
	int i;

	for (i = 0; i < VENDOR_NUM; i++) {
		if (nandx_vendor_table[i] == id)
			return i;
	}

	return NONE;
}

void *get_nandx_timing(enum INTERFACE_TYPE type, u8 timing_type)
{
	switch (type) {
	case INTERFACE_LEGACY:
		if (timing_type > SDR_TIMING_TYPE_NUM)
			return NULL;
		return &nandx_legacy_timing_table[timing_type];

	case INTERFACE_ONFI:
		if (timing_type > ONFI_TIMING_TYPE_NUM)
			return NULL;
		return &nandx_onfi_timing_table[timing_type];

	case INTERFACE_TOGGLE:
		if (timing_type > TOGGLE_TIMING_TYPE_NUM)
			return NULL;
		return &nandx_toggle_timing_table[timing_type];

	default:
		return NULL;
	}

	return NULL;
}
