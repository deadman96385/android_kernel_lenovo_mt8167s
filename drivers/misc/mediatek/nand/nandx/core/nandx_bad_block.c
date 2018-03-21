/*
 * Copyright (C) 2017 MediaTek Inc.
 * Licensed under either
 *     BSD Licence, (see NOTICE for more details)
 *     GNU General Public License, version 2.0, (see NOTICE for more details)
 */

#include "nandx_errno.h"
#include "nandx_device_info.h"
#include "nandx_chip.h"
#include "nandx_chip_common.h"

static int nandx_bad_block_read(struct nandx_chip *chip, u32 row, bool oob)
{
	struct nandx_chip_dev *chip_dev = &chip->chip_dev;
	int ret = NAND_OK;
	u8 *data, *spare;
	bool ecc_enable = true;

	data = mem_alloc(1, chip->nfc->sector_size);
	spare = mem_alloc(1, chip->dev_info->spare_size);

	ecc_enable = chip_dev->get_mode(chip_dev, OPS_MODE_ECC);
	if (!ecc_enable) {
		ret =
		    chip_dev->change_mode(chip_dev, OPS_MODE_ECC, true, NULL);
		goto freebuf;
	}
	nandx_chip_read_page(chip, row);
	nandx_chip_random_output(chip, row, 0);
	ret = nandx_chip_read_data(chip, 1, data, spare);
	if (ret == -ENANDREAD) {
		ret = -ENANDBAD;
		goto freebuf;
	}
	if (!ecc_enable) {
		ret = chip_dev->change_mode(chip_dev, OPS_MODE_ECC, false,
					    NULL);
		goto freebuf;
	}

	if (oob) {
		if (spare[0] != 0xff)
			ret = -ENANDBAD;
	} else {
		if (data[0] != 0xff)
			ret = -ENANDBAD;
	}

freebuf:
	mem_free(data);
	mem_free(spare);
	return ret;
}

static int nandx_bad_block_slc_program(struct nandx_chip *chip, u32 row)
{
	int status;

	chip->slc_ops->entry(chip);
	nandx_chip_program_page(chip, row, NULL, NULL);
	status = nandx_chip_read_status(chip);
	chip->slc_ops->exit(chip);

	return STATUS_SLC_FAIL(status) ? -ENANDBAD : NAND_OK;
}

static int nandx_bad_block_read_upper_page(struct nandx_chip *chip, u32 row)
{
	int i, ret = -ENANDBAD;
	u8 *data;

	data = mem_alloc(1, chip->nfc->sector_size);

	/* upper page */
	row += 2;
	nandx_chip_read_page(chip, row);
	nandx_chip_random_output(chip, row, 0);
	nandx_chip_read_data(chip, 1, data, NULL);

	for (i = 0; i < 12; i++) {
		if (data[i] != 0x00) {
			ret = NAND_OK;
			break;
		}
	}

	mem_free(data);
	return ret;
}

int nandx_bad_block_check(struct nandx_chip *chip, u32 row,
			  enum BAD_BLOCK_TYPE type)
{
	int page_per_block;

	page_per_block =
	    chip->dev_info->block_size / chip->dev_info->page_size;
	row = row / page_per_block * page_per_block;

	switch (type) {
	case BAD_BLOCK_READ_OOB:
		return nandx_bad_block_read(chip, row, true);

	case BAD_BLOCK_READ_PAGE:
		return nandx_bad_block_read(chip, row, false);

	case BAD_BLOCK_SLC_PROGRAM:
		return nandx_bad_block_slc_program(chip, row);

	case BAD_BLOCK_READ_UPPER_PAGE:
		return nandx_bad_block_read_upper_page(chip, row);

	default:
		break;
	}

	return NAND_OK;
}
