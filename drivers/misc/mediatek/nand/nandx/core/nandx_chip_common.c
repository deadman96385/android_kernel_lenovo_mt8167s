/*
 * Copyright (C) 2017 MediaTek Inc.
 * Licensed under either
 *     BSD Licence, (see NOTICE for more details)
 *     GNU General Public License, version 2.0, (see NOTICE for more details)
 */

#include "nandx_chip_common.h"

#define NANDX_CHIP_WAIT_BUSY(nfc, timeout, type, ret) \
do { \
	ret = nfc->wait_busy(nfc, timeout, type); \
	if (ret) \
		pr_err("%s: wait busy timeout!\n", \
			    __func__); \
} while (0)

static u32 nandx_chip_addressing(struct nandx_chip *chip, u32 row)
{
	u32 wl, block, plane, lun;
	u32 page_per_block;
	struct nandx_device_info *dev_info = chip->dev_info;
	struct pair_page_ops *pp_ops = chip->pp_ops;

	page_per_block = dev_info->block_size / dev_info->page_size;
	block = row / page_per_block;

	wl = row % page_per_block;
	if (dev_info->type == NAND_TLC) {
		/* tlc wl number is 1/3 */
		wl = wl / 3;
	} else if (chip->slc_mode && pp_ops) {
		wl = pp_ops->transfer_offset(wl);
	}

	plane = block % dev_info->plane_num;
	lun = (block % (dev_info->plane_num * dev_info->lun_num)) %
	    dev_info->lun_num;

	return get_physical_row_address(chip->addressing, lun, plane, block,
					wl);
}

static void nandx_chip_set_precmd(struct nandx_chip *chip, int cycle)
{
	u8 cmd = NONE;
	struct nfc_handler *nfc = chip->nfc;

	if (chip->slc_mode)
		return;
	cmd = get_extend_cmd(chip->pre_cmd, cycle);

	if (cmd != NONE)
		nfc->send_command(nfc, cmd);
}

void nandx_chip_set_program_order_cmd(struct nandx_chip *chip, int cycle)
{
	u8 cmd = NONE;
	struct nfc_handler *nfc = chip->nfc;

	if (chip->slc_mode)
		return;

	cmd = get_extend_cmd(chip->program_order_cmd, cycle);

	if (cmd != NONE)
		nfc->send_command(nfc, cmd);
}

void nandx_chip_reset(struct nandx_chip *chip)
{
	struct nfc_handler *nfc = chip->nfc;
	int ret;

	nfc->send_command(nfc, chip->cmd[CMD_RESET]);
	/* wait tPOR */
	NANDX_CHIP_WAIT_BUSY(nfc, 40000, POLL_WAIT_RB, ret);
}

void nandx_chip_reset_lun(struct nandx_chip *chip)
{
	struct nfc_handler *nfc = chip->nfc;
	int ret;

	nfc->send_command(nfc, chip->cmd[CMD_RESET_LUN]);
	/* wait tRST */
	NANDX_CHIP_WAIT_BUSY(nfc, 1000, POLL_WAIT_RB, ret);
}

void nandx_chip_synchronous_reset(struct nandx_chip *chip)
{
	struct nfc_handler *nfc = chip->nfc;
	int ret;

	nfc->send_command(nfc, chip->cmd[CMD_RESET_SYNC]);
	/* wait tRST */
	NANDX_CHIP_WAIT_BUSY(nfc, 1000, POLL_WAIT_RB, ret);
}

void nandx_chip_read_id(struct nandx_chip *chip, u8 *id, int num)
{
	int i;
	struct nfc_handler *nfc = chip->nfc;

	nfc->send_command(nfc, chip->cmd[CMD_READ_ID]);
	nfc->send_address(nfc, 0, 0, 1, 0);

	for (i = 0; i < num; i++)
		id[i] = nfc->read_byte(nfc);
}

u8 nandx_chip_read_status(struct nandx_chip *chip)
{
	struct nfc_handler *nfc = chip->nfc;

	nfc->send_command(nfc, chip->cmd[CMD_READ_STATUS]);

	return nfc->read_byte(nfc);
}

u8 nandx_chip_read_enhance_status(struct nandx_chip *chip, u32 row)
{
	u32 addr_cycle;
	struct nfc_handler *nfc = chip->nfc;

	addr_cycle = chip->dev_info->addr_cycle - 2;
	nfc->send_command(nfc, chip->cmd[CMD_READ_ENHANCE_STATUS]);
	nfc->send_address(nfc, 0, row, 0, addr_cycle);

	return nfc->read_byte(nfc);
}

void nandx_chip_set_feature(struct nandx_chip *chip, u8 addr, u8 *param,
			    int num)
{
	int i, temp_num, ret;
	struct nfc_handler *nfc = chip->nfc;
	u8 *temp;

	temp = param;
	temp_num = num;
	if (chip->ddr_enable) {
		temp = mem_alloc(num << 1, sizeof(u8));
		if (!temp)
			return;
		for (i = 0; i < num; i++)
			temp[i << 1] = temp[(i << 1) + 1] = param[i];
		temp_num = num << 1;
	}

	/* should disable randomizer */
	if (chip->randomize)
		nandx_chip_disable_randomizer(chip);

	nfc->send_command(nfc, chip->cmd[CMD_SET_FEATURE]);
	nfc->send_address(nfc, addr, 0, 1, 0);

	for (i = 0; i < temp_num; i++)
		nfc->write_byte(nfc, temp[i]);

	/* wait tFEAT */
	NANDX_CHIP_WAIT_BUSY(nfc, 1000, POLL_WAIT_RB, ret);

	if (chip->ddr_enable)
		mem_free(temp);
}

void nandx_chip_get_feature(struct nandx_chip *chip, u8 addr, u8 *param,
			    int num)
{
	int i, temp_num, ret;
	struct nfc_handler *nfc = chip->nfc;
	u8 *temp;

	temp = param;
	temp_num = num;
	if (chip->ddr_enable) {
		temp = mem_alloc(num << 1, sizeof(u8));
		if (!temp)
			return;
		temp_num = num << 1;
	}

	/* should disable randomizer */
	if (chip->randomize)
		nandx_chip_disable_randomizer(chip);

	nfc->send_command(nfc, chip->cmd[CMD_GET_FEATURE]);
	nfc->send_address(nfc, addr, 0, 1, 0);
	/* wait tFEAT */
	NANDX_CHIP_WAIT_BUSY(nfc, 1000, POLL_WAIT_RB, ret);

	for (i = 0; i < temp_num; i++)
		temp[i] = nfc->read_byte(nfc);

	if (chip->ddr_enable) {
		for (i = 0; i < num; i++)
			param[i] = temp[i << 1];
		mem_free(temp);
	}
}

void nandx_chip_set_feature_with_check(struct nandx_chip *chip, u8 addr,
				       u8 *param, u8 *back, int num)
{
	int i;

	nandx_chip_set_feature(chip, addr, param, num);
	nandx_chip_get_feature(chip, addr, back, num);

	for (i = 0; i < num; i++)
		NANDX_ASSERT(param[i] == back[i]);
}

void nandx_chip_set_lun_feature(struct nandx_chip *chip, u8 lun, u8 addr,
				u8 *param, int num)
{
	int i, temp_num, ret;
	struct nfc_handler *nfc = chip->nfc;
	u8 *temp;

	temp = param;
	temp_num = num;
	if (chip->ddr_enable) {
		temp = mem_alloc(num << 1, sizeof(u8));
		if (!temp)
			return;
		for (i = 0; i < num; i++)
			temp[i << 1] = temp[(i << 1) + 1] = param[i];
		temp_num = num << 1;
	}

	/* should disable randomizer */
	if (chip->randomize)
		nandx_chip_disable_randomizer(chip);

	nfc->send_command(nfc, chip->cmd[CMD_SET_LUN_FEATURE]);
	nfc->send_address(nfc, lun, 0, 1, 0);
	nfc->send_address(nfc, addr, 0, 1, 0);

	for (i = 0; i < temp_num; i++)
		nfc->write_byte(nfc, temp[i]);

	/* wait tFEAT */
	NANDX_CHIP_WAIT_BUSY(nfc, 1000, POLL_WAIT_RB, ret);

	if (chip->ddr_enable)
		mem_free(temp);
}

void nandx_chip_get_lun_feature(struct nandx_chip *chip, u8 lun, u8 addr,
				u8 *param, int num)
{
	int i, temp_num, ret;
	struct nfc_handler *nfc = chip->nfc;
	u8 *temp;

	temp = param;
	temp_num = num;
	if (chip->ddr_enable) {
		temp = mem_alloc(num << 1, sizeof(u8));
		if (!temp)
			return;
		temp_num = num << 1;
	}

	/* should disable randomizer */
	if (chip->randomize)
		nandx_chip_disable_randomizer(chip);

	nfc->send_command(nfc, chip->cmd[CMD_GET_LUN_FEATURE]);
	nfc->send_address(nfc, lun, 0, 1, 0);
	nfc->send_address(nfc, addr, 0, 1, 0);

	/* wait tFEAT */
	NANDX_CHIP_WAIT_BUSY(nfc, 1000, POLL_WAIT_RB, ret);

	for (i = 0; i < temp_num; i++)
		temp[i] = nfc->read_byte(nfc);

	if (chip->ddr_enable) {
		for (i = 0; i < num; i++)
			param[i] = temp[i << 1];
		mem_free(temp);
	}
}

void nandx_chip_set_lun_feature_with_check(struct nandx_chip *chip, u8 lun,
					   u8 addr, u8 *param, u8 *back,
					   int num)
{
	int i;

	nandx_chip_set_lun_feature(chip, lun, addr, param, num);
	nandx_chip_get_lun_feature(chip, lun, addr, back, num);

	for (i = 0; i < num; i++)
		NANDX_ASSERT(param[i] == back[i]);
}

void nandx_chip_erase_block(struct nandx_chip *chip, u32 row)
{
	u32 addr_cycle;
	struct nfc_handler *nfc = chip->nfc;
	int ret;

	row = nandx_chip_addressing(chip, row);

	addr_cycle = chip->dev_info->addr_cycle - 2;
	nfc->send_command(nfc, chip->cmd[CMD_BLOCK_ERASE0]);
	nfc->send_address(nfc, 0, row, 0, addr_cycle);
	nfc->send_command(nfc, chip->cmd[CMD_BLOCK_ERASE1]);

	/* wait tBERS */
	NANDX_CHIP_WAIT_BUSY(nfc, 300000, IRQ_WAIT_RB, ret);
}

void nandx_chip_multi_erase_block(struct nandx_chip *chip, u32 *rows)
{
	int i, ret;
	u32 addr_cycle, physical_row;
	struct nfc_handler *nfc = chip->nfc;

	addr_cycle = chip->dev_info->addr_cycle - 2;

	for (i = 0; i < chip->dev_info->plane_num; i++) {
		physical_row = nandx_chip_addressing(chip, rows[i]);
		nfc->send_command(nfc, chip->cmd[CMD_BLOCK_ERASE0]);
		nfc->send_address(nfc, 0, physical_row, 0, addr_cycle);
	}

	nfc->send_command(nfc, chip->cmd[CMD_BLOCK_ERASE1]);

	/* wait tBERS */
	NANDX_CHIP_WAIT_BUSY(nfc, 300000, IRQ_WAIT_RB, ret);
}

void nandx_chip_read_parameters_page(struct nandx_chip *chip, u8 *data,
				     int size)
{
	int i;
	struct nfc_handler *nfc = chip->nfc;

	nfc->send_command(nfc, chip->cmd[CMD_READ_PARAMETERS_PAGE]);
	nfc->send_address(nfc, 0, 0, 1, 0);

	for (i = 0; i < size; i++)
		data[i] = nfc->read_byte(nfc);
}

void nandx_chip_enable_randomizer(struct nandx_chip *chip, u32 row,
				  bool encode)
{
	struct nfc_handler *nfc = chip->nfc;

	row = row % (chip->dev_info->block_size / chip->dev_info->page_size);

	nfc->enable_randomizer(nfc, row, encode);
}

void nandx_chip_disable_randomizer(struct nandx_chip *chip)
{
	struct nfc_handler *nfc = chip->nfc;

	nfc->disable_randomizer(nfc);
}

int nandx_chip_read_data(struct nandx_chip *chip, int sector_num, void *data,
			 void *fdm)
{
	int ret;
	struct nfc_handler *nfc = chip->nfc;

	ret = nfc->read_sectors(nfc, sector_num, data, fdm);
	return ret;
}

void nandx_chip_read_page(struct nandx_chip *chip, u32 row)
{
	struct nfc_handler *nfc = chip->nfc;
	u8 wl_page_num = chip->chip_dev.info.wl_page_num;
	int ret;

	if (chip->pre_cmd != EXTEND_CMD_NONE)
		nandx_chip_set_precmd(chip, row % wl_page_num);

	row = nandx_chip_addressing(chip, row);

	nfc->send_command(nfc, chip->cmd[CMD_READ_1ST]);
	nfc->send_address(nfc, 0, row, 2, chip->dev_info->addr_cycle - 2);
	nfc->send_command(nfc, chip->cmd[CMD_READ_2ND]);

	/* wait tR */
	NANDX_CHIP_WAIT_BUSY(nfc, 1000, POLL_WAIT_RB, ret);
}

void nandx_chip_cache_read_page(struct nandx_chip *chip, u32 row)
{
	struct nfc_handler *nfc = chip->nfc;
	u8 wl_page_num = chip->chip_dev.info.wl_page_num;
	int ret;

	if (chip->pre_cmd != EXTEND_CMD_NONE)
		nandx_chip_set_precmd(chip, row % wl_page_num);

	row = nandx_chip_addressing(chip, row);

	nfc->send_command(nfc, chip->cmd[CMD_READ_1ST]);
	nfc->send_address(nfc, 0, row, 2, chip->dev_info->addr_cycle - 2);
	nfc->send_command(nfc, chip->cmd[CMD_CACHE_READ_2ND]);

	/* wait tRCBSY */
	NANDX_CHIP_WAIT_BUSY(nfc, 1000, POLL_WAIT_RB, ret);
}

void nandx_chip_cache_read_last_page(struct nandx_chip *chip)
{
	struct nfc_handler *nfc = chip->nfc;
	int ret;

	nfc->send_command(nfc, chip->cmd[CMD_CACHE_READ_LAST]);
	/* wait tRCBSY */
	NANDX_CHIP_WAIT_BUSY(nfc, 1000, POLL_WAIT_RB, ret);
}

void nandx_chip_multi_read_page(struct nandx_chip *chip, u32 row)
{
	struct nfc_handler *nfc = chip->nfc;
	u8 wl_page_num = chip->chip_dev.info.wl_page_num;
	int ret;

	if (chip->pre_cmd != EXTEND_CMD_NONE)
		nandx_chip_set_precmd(chip, row % wl_page_num);

	row = nandx_chip_addressing(chip, row);

	nfc->send_command(nfc, chip->cmd[CMD_READ_1ST]);
	nfc->send_address(nfc, 0, row, 2, chip->dev_info->addr_cycle - 2);
	nfc->send_command(nfc, chip->cmd[CMD_MULTI_READ_2ND]);

	/* wait tDBSY */
	NANDX_CHIP_WAIT_BUSY(nfc, 500, POLL_WAIT_RB, ret);
}

void nandx_chip_random_output(struct nandx_chip *chip, u32 row, u32 col)
{
	struct nfc_handler *nfc = chip->nfc;
	int ret;

	NANDX_ASSERT((col % nfc->sector_size) == 0);
	col = col / nfc->sector_size * (nfc->sector_size + nfc->spare_size);

	row = nandx_chip_addressing(chip, row);

	nfc->send_command(nfc, chip->cmd[CMD_RANDOM_OUT_1ST]);
	if (chip->vendor_type == VENDOR_MICRON)
		nfc->send_address(nfc, col, 0, 2, 0);
	else
		nfc->send_address(nfc, col, row, 2,
				  chip->dev_info->addr_cycle - 2);
	nfc->send_command(nfc, chip->cmd[CMD_RANDOM_OUT_2ND]);

	/* wait tWHR2 or tCCS */
	NANDX_CHIP_WAIT_BUSY(nfc, 400, POLL_WAIT_TWHR2, ret);
}

int nandx_chip_program_page(struct nandx_chip *chip, u32 row, void *data,
			    void *fdm)
{
	int ret;
	struct nfc_handler *nfc = chip->nfc;
	u8 wl_page_num = chip->chip_dev.info.wl_page_num;

	if (chip->pre_cmd != EXTEND_CMD_NONE)
		nandx_chip_set_precmd(chip, row % wl_page_num);

	row = nandx_chip_addressing(chip, row);

	nfc->send_command(nfc, chip->cmd[CMD_PROGRAM_1ST]);
	nfc->send_address(nfc, 0, row, 2, chip->dev_info->addr_cycle - 2);
	ret = nfc->write_page(nfc, data, fdm);
	if (ret)
		goto err;
	nfc->send_command(nfc, chip->cmd[CMD_PROGRAM_2ND]);

	/* wait tPROG */
	NANDX_CHIP_WAIT_BUSY(nfc, 25000, IRQ_WAIT_RB, ret);

err:
	return ret;
}

int nandx_chip_cache_program_page(struct nandx_chip *chip, u32 row,
				  void *data, void *fdm)
{
	int ret;
	struct nfc_handler *nfc = chip->nfc;
	u8 wl_page_num = chip->chip_dev.info.wl_page_num;

	if (chip->pre_cmd != EXTEND_CMD_NONE)
		nandx_chip_set_precmd(chip, row % wl_page_num);

	row = nandx_chip_addressing(chip, row);

	nfc->send_command(nfc, chip->cmd[CMD_PROGRAM_1ST]);
	nfc->send_address(nfc, 0, row, 2, chip->dev_info->addr_cycle - 2);
	ret = nfc->write_page(nfc, data, fdm);
	if (ret)
		goto err;
	nfc->send_command(nfc, chip->cmd[CMD_CACHE_PROGRAM_2ND]);

	/* wait MAX(tPBSY, tCBSY) */
	NANDX_CHIP_WAIT_BUSY(nfc, 25000, IRQ_WAIT_RB, ret);

err:
	return ret;
}

int nandx_chip_multi_program_1stpage(struct nandx_chip *chip, u32 row,
				     void *data, void *fdm)
{
	int ret;
	struct nfc_handler *nfc = chip->nfc;
	u8 wl_page_num = chip->chip_dev.info.wl_page_num;

	if (chip->pre_cmd != EXTEND_CMD_NONE)
		nandx_chip_set_precmd(chip, row % wl_page_num);

	row = nandx_chip_addressing(chip, row);

	nfc->send_command(nfc, chip->cmd[CMD_PROGRAM_1ST]);
	nfc->send_address(nfc, 0, row, 2, chip->dev_info->addr_cycle - 2);
	ret = nfc->write_page(nfc, data, fdm);
	if (ret)
		goto err;
	nfc->send_command(nfc, chip->cmd[CMD_MULTI_PROGRAM_2ND]);

	/* wait MAX(tPBSY, tDBSY) */
	NANDX_CHIP_WAIT_BUSY(nfc, 2000, POLL_WAIT_RB, ret);

err:
	return ret;
}

int nandx_chip_multi_program_2ndpage(struct nandx_chip *chip, u32 row,
				     void *data, void *fdm)
{
	int ret;
	struct nfc_handler *nfc = chip->nfc;
	u8 wl_page_num = chip->chip_dev.info.wl_page_num;

	if (chip->pre_cmd != EXTEND_CMD_NONE)
		nandx_chip_set_precmd(chip, row % wl_page_num);

	row = nandx_chip_addressing(chip, row);

	nfc->send_command(nfc, chip->cmd[CMD_MULTI2_PROGRAM_1ST]);
	nfc->send_address(nfc, 0, row, 2, chip->dev_info->addr_cycle - 2);
	ret = nfc->write_page(nfc, data, fdm);
	if (ret)
		goto err;
	nfc->send_command(nfc, chip->cmd[CMD_MULTI2_PROGRAM_2ND]);

	/* wait tPROG */
	NANDX_CHIP_WAIT_BUSY(nfc, 25000, IRQ_WAIT_RB, ret);

err:
	return ret;
}

int nandx_chip_calibration(struct nandx_chip *chip)
{
	int ret = 0;
	u8 back = 0xff;
	u8 feature = chip->feature[FEATURE_DRIVE_STRENGTH];
	u8 *strength;
	static int drive_level;

	drive_level++;
	if (drive_level == DRIVE_LEVEL_NUM ||
	    chip->drive_strength[drive_level] == NONE) {
		drive_level = 0;
		ret = -1;
	}

	strength = &chip->drive_strength[drive_level];
	nandx_chip_set_feature_with_check(chip, feature, strength, &back, 1);

	return ret;
}
