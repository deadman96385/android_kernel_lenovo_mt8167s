/*
 * Copyright (C) 2017 MediaTek Inc.
 * Licensed under either
 *     BSD Licence, (see NOTICE for more details)
 *     GNU General Public License, version 2.0, (see NOTICE for more details)
 */
#include "nandx_util.h"
#include "nandx_errno.h"
#include "nandx_info.h"
#include "nandx_core.h"
#include "nandx_ops.h"
#include "nandx_bmt.h"

#define BMT_VERSION		(1)	/* initial version */
#define MAIN_BMT_SIZE		(0x200)
#define SIGNATURE_SIZE		(3)
#define MAIN_SIGNATURE_OFFSET	(0)
#define OOB_SIGNATURE_OFFSET	(1)
#define OOB_INDEX_OFFSET	(29)
#define OOB_INDEX_SIZE		(2)
#define FAKE_INDEX		(0xAAAA)

struct bmt_header {
	char signature[SIGNATURE_SIZE];
	u8 version;
	u8 bad_count;		/* bad block count in pool */
	u8 mapped_count;	/* mapped block count in pool */
	u8 checksum;
	u8 reseverd[13];
};

struct bmt_entry {
	u16 bad_index;		/* bad block index */
	u16 mapped_index;	/* mapping block index in the replace pool */
};

struct bmt_data_info {
	struct bmt_header header;
	struct bmt_entry table[MAIN_BMT_SIZE];
#ifdef CONFIG_MNTL_SUPPORT
	struct data_bmt_struct data_bmt;
#endif
};

struct bmt_handler {
	struct nandx_chip_info *dev_info;
	u32 start_block;
	u32 block_count;
	u32 current_block;
	u8 *data;
	u8 *oob;
	struct bmt_data_info data_info;
};

static const char MAIN_SIGNATURE[] = "BMT";
static const char OOB_SIGNATURE[] = "bmt";

static struct bmt_handler *nandx_bmt;

static bool bmt_read_page(struct bmt_handler *bmt, u32 page)
{
	int ret;
	struct nandx_ops ops;

	nandx_get_device(FL_READING);

	ops.row = page;
	ops.col = 0;
	ops.len = bmt->dev_info->page_size;
	ops.data = bmt->data;
	ops.oob = bmt->oob;
	ret = nandx_core_read(&ops, 1, MODE_SLC);

	nandx_release_device();
	return ret < 0 ? false : true;
}

static bool bmt_write_page(struct bmt_handler *bmt, u32 page)
{
	int ret;
	struct nandx_ops ops;

	nandx_get_device(FL_WRITING);

	ops.row = page;
	ops.col = 0;
	ops.len = bmt->dev_info->page_size;
	ops.data = bmt->data;
	ops.oob = bmt->oob;
	ret = nandx_core_write(&ops, 1, MODE_SLC);

	nandx_release_device();
	return ret < 0 ? false : true;
}

static bool bmt_erase_block(struct bmt_handler *bmt, u32 block)
{
	int ret;
	u32 row;

	nandx_get_device(FL_ERASING);

	row = block * (bmt->dev_info->block_size / bmt->dev_info->page_size);
	ret = nandx_core_erase(&row, 1, MODE_SLC);

	nandx_release_device();
	return ret < 0 ? false : true;
}

static bool bmt_block_is_bad(struct bmt_handler *bmt, u32 block)
{
	return false;
#if 0
	u32 row;
	int ret;

	nandx_get_device(FL_READING);
	row = block * (bmt->dev_info->block_size / bmt->dev_info->page_size);
	ret = nandx_core_is_bad(row);
	nandx_release_device();

	return ret;
#endif
}

static bool bmt_mark_block_bad(struct bmt_handler *bmt, u32 block)
{
	int ret;
	u32 row;

	nandx_get_device(FL_WRITING);
	row = block * (bmt->dev_info->block_size / bmt->dev_info->page_size);
	ret = nandx_core_mark_bad(row);
	nandx_release_device();

	return ret < 0 ? false : true;
}

static void bmt_dump_info(struct bmt_data_info *data_info)
{
	int i;

	pr_info("BMT v%d. total %d mapping:\n",
		data_info->header.version, data_info->header.mapped_count);
	for (i = 0; i < data_info->header.mapped_count; i++) {
		pr_info("block[%d] map to block[%d]\n",
			data_info->table[i].bad_index,
			data_info->table[i].mapped_index);
	}
}

static bool bmt_match_signature(struct bmt_handler *bmt)
{

	return memcmp(bmt->data + MAIN_SIGNATURE_OFFSET, MAIN_SIGNATURE,
		      SIGNATURE_SIZE) ? false : true;
}

static u8 bmt_calculate_checksum(struct bmt_handler *bmt)
{
	struct bmt_data_info *data_info = &bmt->data_info;
	u32 i, size = bmt->block_count * sizeof(struct bmt_entry);
	u8 checksum = 0;
	u8 *data = (u8 *)data_info->table;

	checksum += data_info->header.version;
	checksum += data_info->header.mapped_count;

	for (i = 0; i < size; i++)
		checksum += data[i];

	return checksum;
}

static int bmt_block_is_mapped(struct bmt_data_info *data_info, u32 index)
{
	int i;

	for (i = 0; i < data_info->header.mapped_count; i++) {
		if (index == data_info->table[i].mapped_index)
			return i;
	}
	return -1;
}

static bool bmt_data_is_valid(struct bmt_handler *bmt)
{
	int i;
	u8 checksum;
	struct bmt_data_info *data_info = &bmt->data_info;
	struct bmt_entry *table = data_info->table;
	struct nandx_chip_info *dev_info = bmt->dev_info;

	checksum = bmt_calculate_checksum(bmt);

	/* checksum correct? */
	if (data_info->header.checksum != checksum) {
		pr_err("BMT Data checksum error: %x %x\n",
		       data_info->header.checksum, checksum);
		return false;
	}

	pr_debug("BMT Checksum is: 0x%x\n", data_info->header.checksum);

	/* block index correct? */
	for (i = 0; i < data_info->header.mapped_count; i++) {
		if (table->bad_index >= dev_info->block_num ||
		    table->mapped_index >= dev_info->block_num ||
		    table->mapped_index < bmt->start_block) {
			pr_err("err: bad_%d, mapped %d\n",
			       table->bad_index, table->mapped_index);
			return false;
		}
		table++;
	}

	/* pass check, valid bmt. */
	pr_debug("Valid BMT, version v%d\n", data_info->header.version);

	return true;
}

static void bmt_fill_buffer(struct bmt_handler *bmt)
{
	struct bmt_data_info *data_info = &bmt->data_info;

	bmt_dump_info(&bmt->data_info);

	memcpy(data_info->header.signature, MAIN_SIGNATURE, SIGNATURE_SIZE);
	data_info->header.version = BMT_VERSION;

	data_info->header.checksum = bmt_calculate_checksum(bmt);

	memcpy(bmt->data + MAIN_SIGNATURE_OFFSET, data_info,
	       sizeof(struct bmt_data_info));
	memcpy(bmt->oob + OOB_SIGNATURE_OFFSET, OOB_SIGNATURE,
	       SIGNATURE_SIZE);
}

/* return valid index if found BMT, else return 0 */
static int bmt_load_data(struct bmt_handler *bmt)
{
	struct nandx_chip_info *dev_info = bmt->dev_info;
	u32 row_addr;
	u32 bmt_index = bmt->start_block + bmt->block_count - 1;

	for (; bmt_index >= bmt->start_block; bmt_index--) {
		if (bmt_block_is_bad(bmt, bmt_index)) {
			pr_err("Skip bad block: %d\n", bmt_index);
			continue;
		}

		row_addr = dev_info->block_size / dev_info->page_size;
		row_addr = bmt_index * row_addr;
		if (!bmt_read_page(bmt, row_addr)) {
			pr_err("read block %d error\n", bmt_index);
			continue;
		}

		memcpy(&bmt->data_info, bmt->data + MAIN_SIGNATURE_OFFSET,
		       sizeof(struct bmt_data_info));

		if (!bmt_match_signature(bmt))
			continue;

		pr_info("Match bmt signature @ block 0x%x\n", bmt_index);

		if (!bmt_data_is_valid(bmt)) {
			pr_err("BMT data not correct %d\n", bmt_index);
			continue;
		}

		bmt_dump_info(&bmt->data_info);
		return bmt_index;
	}

	pr_err("Bmt block not found!\n");

	return 0;
}

/*
 * Find an available block and erase.
 * start_from_end: if true, find available block from end of flash.
 *                 else, find from the beginning of the pool
 * need_erase: if true, all unmapped blocks in the pool will be erased
 */
static int find_available_block(struct bmt_handler *bmt, bool start_from_end)
{
	u32 i, block;
	int direct;

	pr_debug("Try to find_available_block\n");

	block = start_from_end ? (bmt->dev_info->block_num - 1) :
	    bmt->start_block;
	direct = start_from_end ? -1 : 1;

	for (i = 0; i < bmt->block_count; i++, block += direct) {
		if (block == bmt->current_block) {
			pr_debug("Skip bmt block 0x%x\n", block);
			continue;
		}

		if (bmt_block_is_bad(bmt, block)) {
			pr_debug("Skip bad block 0x%x\n", block);
			continue;
		}

		if (bmt_block_is_mapped(&bmt->data_info, block) >= 0) {
			pr_debug("Skip mapped block 0x%x\n", block);
			continue;
		}

		pr_debug("Find block 0x%x available\n", block);

		return block;
	}

	return 0;
}

static u16 get_bad_index_from_oob(struct bmt_handler *bmt, u8 *oob)
{
	u16 index;
	u32 offset = OOB_INDEX_OFFSET;

	if (KB(2) == bmt->dev_info->page_size)
		offset = 13;

	memcpy(&index, oob + offset, OOB_INDEX_SIZE);

	return index;
}

static void set_bad_index_to_oob(struct bmt_handler *bmt, u8 *oob, u16 index)
{
	u32 offset = OOB_INDEX_OFFSET;

	if (KB(2) == bmt->dev_info->page_size)
		offset = 13;

	memcpy(oob + offset, &index, sizeof(index));
}

static bool bmt_write_to_flash(struct bmt_handler *bmt)
{
	bool ret;
	u32 page, page_size, block_size;

	page_size = bmt->dev_info->page_size;
	block_size = bmt->dev_info->block_size;

	pr_debug("Try to write BMT\n");

	/* fill NAND BMT buffer */
	memset(bmt->oob, 0xFF, bmt->dev_info->oob_size);
	bmt_fill_buffer(bmt);

	while (1) {
		if (bmt->current_block == 0) {
			bmt->current_block = find_available_block(bmt, true);
			if (!bmt->current_block) {
				pr_err("no valid block\n");
				return false;
			}
		}

		pr_debug("Find BMT block: 0x%x\n", bmt->current_block);

		/* write bmt to flash */
		ret = bmt_erase_block(bmt, bmt->current_block);
		if (ret == true) {
			page = bmt->current_block * (block_size / page_size);
			ret = bmt_write_page(bmt, page);
			if (ret == true)
				break;
			pr_err("write failed 0x%x\n", bmt->current_block);
		}

		pr_err("erase fail, mark bad 0x%x\n", bmt->current_block);
		bmt_mark_block_bad(bmt, bmt->current_block);

		bmt->current_block = 0;
	}

	pr_debug("Write BMT data to block 0x%x success\n",
		 bmt->current_block);

	return true;
}

/*
 * Reconstruct bmt, called when found bmt info doesn't match bad
 * block info in flash.
 *
 * Return NULL for failure
 */
static int bmt_construct(struct bmt_handler *bmt)
{
	struct nandx_chip_info *dev_info = bmt->dev_info;
	int ret;
	u32 index, bad_index, row_addr, count = 0;
	int mapped_block;
	struct bmt_data_info *data_info = &bmt->data_info;
	u32 end_block = bmt->start_block + bmt->block_count;
	u32 ppb = dev_info->block_size / dev_info->page_size;

	/* init everything in BMT struct */
	data_info->header.version = BMT_VERSION;
	data_info->header.bad_count = 0;
	data_info->header.mapped_count = 0;

	memset(data_info->table, 0,
	       bmt->block_count * sizeof(struct bmt_entry));

	for (index = bmt->start_block; index < end_block; index++) {
		if (bmt_block_is_bad(bmt, index)) {
			pr_err("Skip bad block: 0x%x\n", index);
			continue;
		}

		row_addr = index * ppb;
		bmt_read_page(bmt, row_addr);
		bad_index = get_bad_index_from_oob(bmt, bmt->oob);
		if (bad_index >= bmt->start_block) {
			pr_err("get bad index: 0x%x\n", bad_index);
			if (bad_index != 0xFFFF)
				pr_err("Invalid index\n");
			continue;
		}

		pr_debug("0x%x mapped to bad block: 0x%x\n",
			 index, bad_index);

		if (!bmt_block_is_bad(bmt, index)) {
			pr_debug("block 0x%x not bad\n", bad_index);
			continue;
		}

		mapped_block = bmt_block_is_mapped(data_info, bad_index);
		if (mapped_block >= 0) {
			data_info->table[mapped_block].mapped_index = index;
		} else {
			/* add mapping to BMT */
			data_info->table[count].bad_index = bad_index;
			data_info->table[count].mapped_index = index;
			count++;
		}

		pr_debug("Add mapping: 0x%x -> 0x%x to BMT\n",
			 bad_index, index);

	}

	data_info->header.mapped_count = count;
	pr_debug("Scan replace pool done, mapped block: %d\n",
		 data_info->header.mapped_count);

	/* write BMT back */
	ret = bmt_write_to_flash(bmt);
	if (!ret) {
		pr_warn("no good block to write BMT\n");
		return ret;
	}

	return 0;
}

/*
 * [BMT Interface]
 *
 * Description:
 *	Init bmt from nand. Reconstruct if not found or data error
 *
 * Parameter:
 *	size: size of bmt and replace pool
 *
 * Return:
 *	NULL for failure, and a bmt struct for success
 */
int nandx_bmt_init(struct nandx_chip_info *dev_info, u32 block_num)
{
	int ret;

	if (block_num > MAIN_BMT_SIZE) {
		pr_err("bmt size %d over %d\n", block_num, MAIN_BMT_SIZE);
		return -ENOMEM;
	}

	nandx_bmt = mem_alloc(1, sizeof(struct bmt_handler));
	if (!nandx_bmt)
		return -ENOMEM;

	nandx_bmt->dev_info = dev_info;
	nandx_bmt->data = mem_alloc(1, dev_info->page_size);
	nandx_bmt->oob = mem_alloc(1, dev_info->oob_size);
	nandx_bmt->start_block = dev_info->block_num - block_num;
	nandx_bmt->block_count = block_num;

	pr_info("bmt start block: %d, bmt block count: %d\n",
		nandx_bmt->start_block, nandx_bmt->block_count);

	memset(nandx_bmt->data_info.table, 0,
	       block_num * sizeof(struct bmt_entry));
	nandx_bmt->current_block = bmt_load_data(nandx_bmt);
	if (!nandx_bmt->current_block) {
		ret = bmt_construct(nandx_bmt);
		if (ret)
			return 0;
		return -1;
	}
	return 0;
}

void nandx_bmt_exit(void)
{
	mem_free(nandx_bmt->data);
	mem_free(nandx_bmt->oob);
	mem_free(nandx_bmt);
}

u32 nandx_bmt_update(u32 bad_block, enum UPDATE_REASON reason)
{
	u32 i, map_index = 0, map_count = 0;
	struct bmt_handler *bmt = nandx_bmt;
	struct bmt_data_info *data_info = &bmt->data_info;
	struct bmt_entry *entry;
#ifdef CONFIG_MNTL_SUPPORT
	struct data_bmt_struct *info = &(bmt->data_info.data_bmt);

	if (bad_block >= info->start_block &&
	    bad_block < info->end_block) {
		if (nandx_bmt_get_mapped_block(bad_block) != DATA_BAD_BLK) {
			pr_debug("update_DATA bad block %d\n", bad_block);
			info->entry[info->bad_count].bad_index = bad_block;
			info->entry[info->bad_count].flag = reason;

			info->bad_count++;
		} else {
			pr_err("block(%d) in bad list\n", bad_block);
			return bad_block;
		}
	} else
#endif
	{
		map_index = find_available_block(bmt, false);
		if (!map_index) {
			pr_err("%s: no good block\n", __func__);
			return bad_block;
		}

		/* now let's update BMT */
		if (bad_block >= bmt->start_block) {
			/* mapped block become bad, find original bad block */
			for (i = 0; i < bmt->block_count; i++) {
				entry = &data_info->table[i];
				if (entry->mapped_index == bad_block) {
					pr_debug("block %d is bad\n",
						 entry->bad_index);
					break;
				}
			}

			entry->mapped_index = map_index;
		} else {
			map_count = data_info->header.mapped_count;
			entry = &data_info->table[map_count];
			entry->mapped_index = map_index;
			entry->bad_index = bad_block;
			data_info->header.mapped_count++;
		}
	}

	if (!bmt_write_to_flash(bmt))
		return bad_block;

	return map_index;
}

void nandx_bmt_update_oob(u32 block, u8 *oob)
{
	u16 index;
	u32 mapped_block;
	struct bmt_handler *bmt = nandx_bmt;

	mapped_block = nandx_bmt_get_mapped_block(block);
	index = mapped_block == block ? FAKE_INDEX : block;

	set_bad_index_to_oob(bmt, oob, index);
}

/*
 * [BMT Interface]
 *
 * Description:
 *	Given an block index, return mapped index if it's mapped, else
 *	return given index.
 *
 * Parameter:
 *	index: given an block index. This value cannot exceed
 *	system_block_count.
 *
 * Return NULL for failure
 */
u32 nandx_bmt_get_mapped_block(u32 block)
{
	int i;
	struct bmt_handler *bmt = nandx_bmt;
	struct bmt_data_info *data_info = &bmt->data_info;
#ifdef CONFIG_MNTL_SUPPORT
	struct data_bmt_struct *data_bmt_info = &(bmt->data_info.data_bmt);
#endif

	if (block >= bmt->start_block)
		return block;

#ifdef CONFIG_MNTL_SUPPORT
	if ((block >= data_bmt_info->start_block) &&
	    (block < data_bmt_info->end_block)) {
		for (i = 0; i < data_bmt_info->bad_count; i++) {
			if (data_bmt_info->entry[i].bad_index == block) {
				pr_debug
				    ("FTL bad block at 0x%x, bad_count:%d\n",
				     block, data_bmt_info->bad_count);
				return DATA_BAD_BLK;
			}
		}
	} else
#endif
	{
		for (i = 0; i < data_info->header.mapped_count; i++) {
			if (data_info->table[i].bad_index == block)
				return data_info->table[i].mapped_index;
		}
	}

	return block;
}

#ifdef CONFIG_MNTL_SUPPORT
int nandx_bmt_get_data_bmt(struct data_bmt_struct *data_bmt)
{
	struct bmt_handler *bmt = nandx_bmt;
	struct data_bmt_struct *data_bmt_info = &(bmt->data_info.data_bmt);

	if (data_bmt_info->version == DATA_BMT_VERSION) {
		memcpy(data_bmt, data_bmt_info,
		       sizeof(struct data_bmt_struct));
		return 0;
	}

	return 1;
}
#endif
