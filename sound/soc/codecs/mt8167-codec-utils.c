/*
 * mt8167-codec-utils.c  --  MT8167 codec driver utility functions
 *
 * Copyright (c) 2016 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <sound/soc.h>
#include <linux/dma-mapping.h>
#include "mt8167-codec.h"
#include "mt8167-codec-utils.h"

enum auxadc_channel_id {
	AUXADC_CH_AU_HPL = 7,
	AUXADC_CH_AU_HPR,
};

struct reg_setting {
	uint32_t reg;
	uint32_t mask;
	uint32_t val;
};

static const struct reg_setting mt8167_codec_cali_setup_regs[] = {
	{
		.reg = AUDIO_TOP_CON0,
		.mask = GENMASK(26, 25),
		.val = 0x0,
	},
	{
		.reg = AFE_MEMIF_PBUF_SIZE,
		.mask = GENMASK(17, 16),
		.val = 0x0,
	},
	{
		.reg = AFE_CONN_24BIT,
		.mask = GENMASK(4, 3),
		.val = 0x0,
	},
	{
		.reg = AFE_DAC_CON1,
		.mask = GENMASK(3, 0),
		.val = 0x9,
	},
	{
		.reg = AFE_CONN1,
		.mask = BIT(21),
		.val = BIT(21),
	},
	{
		.reg = AFE_CONN2,
		.mask = BIT(6),
		.val = BIT(6),
	},
	{
		.reg = AFE_DAC_CON0,
		.mask = BIT(1),
		.val = BIT(1),
	},
	{
		.reg = AFE_ADDA_PREDIS_CON0,
		.mask = GENMASK(31, 0),
		.val = 0x0,
	},
	{
		.reg = AFE_ADDA_PREDIS_CON1,
		.mask = GENMASK(31, 0),
		.val = 0x0,
	},
	{
		.reg = AFE_ADDA_DL_SRC2_CON0,
		.mask = GENMASK(31, 0),
		.val = (0x7 << 28) | (0x03 << 24) | (0x03 << 11) | BIT(1),
	},
	{
		.reg = AFE_ADDA_DL_SRC2_CON1,
		.mask = GENMASK(31, 0),
		.val = 0xf74f0000,
	},
	{
		.reg = AFE_I2S_CON1,
		.mask = (0x9 << 8),
		.val = (0x9 << 8),
	},
	{
		.reg = AFE_ADDA_DL_SRC2_CON0,
		.mask = BIT(0),
		.val = BIT(0),
	},
	{
		.reg = AFE_I2S_CON1,
		.mask = BIT(0),
		.val = BIT(0),
	},
	{
		.reg = AFE_ADDA_UL_DL_CON0,
		.mask = BIT(0),
		.val = BIT(0),
	},
	{
		.reg = AFE_DAC_CON0,
		.mask = BIT(0),
		.val = BIT(0),
	},
};

static const struct reg_setting mt8167_codec_cali_cleanup_regs[] = {
	{
		.reg = AFE_DAC_CON0,
		.mask = BIT(1),
		.val = 0x0,
	},
	{
		.reg = AFE_ADDA_DL_SRC2_CON0,
		.mask = BIT(0),
		.val = 0x0,
	},
	{
		.reg = AFE_I2S_CON1,
		.mask = BIT(0),
		.val = 0x0,
	},
	{
		.reg = AFE_ADDA_UL_DL_CON0,
		.mask = BIT(0),
		.val = 0x0,
	},
	{
		.reg = AFE_DAC_CON0,
		.mask = BIT(0),
		.val = 0x0,
	},
	{
		.reg = AUDIO_TOP_CON0,
		.mask = GENMASK(26, 25),
		.val = (0x3 << 25),
	},
};

static const struct reg_setting mt8167_codec_cali_enable_regs[] = {
	{
		.reg = ABB_AFE_CON3,
		.mask = GENMASK(31, 0),
		.val = 0x0,
	},
	{
		.reg = ABB_AFE_CON4,
		.mask = GENMASK(31, 0),
		.val = 0x0,
	},
	{
		.reg = ABB_AFE_CON10,
		.mask = BIT(0),
		.val = BIT(0),
	},
	{
		.reg = ABB_AFE_CON0,
		.mask = BIT(0),
		.val = BIT(0),
	},
	{
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(27) | BIT(17) | BIT(16),
		.val = BIT(27) | BIT(17) | BIT(16),
	},
	{
		.reg = AUDIO_CODEC_CON00,
		.mask = BIT(19),
		.val = BIT(19),
	},
	{
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11),
		.val = BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11),
	},
	{
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(28) | BIT(25) | BIT(22),
		.val = BIT(28),
	},
};

static const struct reg_setting mt8167_codec_cali_disable_regs[] = {
	{
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(28) | BIT(25) | BIT(22),
		.val = BIT(25) | BIT(22),
	},
	{
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11),
		.val = 0x0,
	},
	{
		.reg = AUDIO_CODEC_CON00,
		.mask = BIT(19),
		.val = 0x0,
	},
	{
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(27) | BIT(17) | BIT(16),
		.val = 0x0,
	},
};

static int mt8167_codec_apply_reg_setting(struct snd_soc_codec *codec,
	const struct reg_setting regs[], uint32_t reg_nums)
{
	int i;

	for (i = 0 ; i < reg_nums ; i++)
		snd_soc_update_bits(codec,
			regs[i].reg, regs[i].mask, regs[i].val);

	return 0;
}

#define AFIFO_SIZE (48 * 1024)
static int mt8167_codec_setup_cali_path(struct snd_soc_codec *codec,
	struct snd_dma_buffer *buf)
{
	/* enable power */
	/* enable clock */
	/* allocate buffer */
	buf->area = dma_zalloc_coherent(codec->dev, AFIFO_SIZE,
		&buf->addr, GFP_KERNEL);

	snd_soc_update_bits(codec,
			AFE_DL1_BASE,
			GENMASK(31, 0),
			buf->addr);
	snd_soc_update_bits(codec,
			AFE_DL1_END,
			GENMASK(31, 0),
			buf->addr + (AFIFO_SIZE - 1));

	/* setup */
	return mt8167_codec_apply_reg_setting(codec,
			mt8167_codec_cali_setup_regs,
			ARRAY_SIZE(mt8167_codec_cali_setup_regs));
}

static int mt8167_codec_cleanup_cali_path(struct snd_soc_codec *codec,
	struct snd_dma_buffer *buf)
{
	int ret = 0;

	/* cleanup */
	ret = mt8167_codec_apply_reg_setting(codec,
			mt8167_codec_cali_cleanup_regs,
			ARRAY_SIZE(mt8167_codec_cali_cleanup_regs));

	/* free buffer */
	dma_free_coherent(codec->dev, AFIFO_SIZE, buf->area,
		buf->addr);
	/* disable power */
	/* disable clock */

	return ret;
}

static int mt8167_codec_enable_cali_path(struct snd_soc_codec *codec)
{
	/* enable */
	return mt8167_codec_apply_reg_setting(codec,
			mt8167_codec_cali_enable_regs,
			ARRAY_SIZE(mt8167_codec_cali_enable_regs));
}

static int mt8167_codec_disable_cali_path(struct snd_soc_codec *codec)
{
	/* disable */
	return mt8167_codec_apply_reg_setting(codec,
			mt8167_codec_cali_disable_regs,
			ARRAY_SIZE(mt8167_codec_cali_disable_regs));
}

static uint32_t mt8167_codec_get_hp_cali_val(struct snd_soc_codec *codec,
	uint32_t auxadc_channel)
{
	uint32_t cali_val = 0;
	struct snd_dma_buffer dma_buf;
#ifdef CONFIG_MTK_AUXADC
	int32_t auxadc_on_val = 0;
	int32_t auxadc_off_val = 0;
	int32_t auxadc_data[4];
#endif

	dev_dbg(codec->dev, "%s\n", __func__);

	mt8167_codec_setup_cali_path(codec, &dma_buf);

#ifdef CONFIG_MTK_AUXADC
	IMM_GetOneChannelValue(auxadc_channel, auxadc_data, &auxadc_off_val);
	dev_dbg(codec->dev, "%s auxadc_off_val: 0x%x\n",
		__func__, auxadc_off_val);
#endif

	mt8167_codec_enable_cali_path(codec);

#ifdef CONFIG_MTK_AUXADC
	/* TODO: calibration process */
	IMM_GetOneChannelValue(auxadc_channel, auxadc_data, &auxadc_on_val);
	dev_dbg(codec->dev, "%s auxadc_on_val: 0x%x\n",
		__func__, auxadc_on_val);
#endif
	mt8167_codec_disable_cali_path(codec);
	mt8167_codec_cleanup_cali_path(codec, &dma_buf);

	return cali_val;
}

uint32_t mt8167_codec_get_hpl_cali_val(struct snd_soc_codec *codec)
{
	return mt8167_codec_get_hp_cali_val(codec, AUXADC_CH_AU_HPL);
}
EXPORT_SYMBOL_GPL(mt8167_codec_get_hpl_cali_val);

uint32_t mt8167_codec_get_hpr_cali_val(struct snd_soc_codec *codec)
{
	return mt8167_codec_get_hp_cali_val(codec, AUXADC_CH_AU_HPR);
}
EXPORT_SYMBOL_GPL(mt8167_codec_get_hpr_cali_val);
