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
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include "mt8167-codec.h"
#include "mt8167-codec-utils.h"
#ifdef CONFIG_MTK_SPEAKER
#include "mt6392-codec.h"
#endif
#ifdef CONFIG_MTK_AUXADC
#include "mtk_auxadc.h"
#endif

/*
 * dc compensation value = dc offset (mV) * SDM gain / (Vout / 2)
 *
 * Vout = DAC output swing * Gbuf
 * Vdac = DAC output swing = 2 * 1.7 * 0.8 * Gsdm
 *
 * Gbuf    : -2 dB -> 10^(-2/20) = 0.79433
 * Gbuf    :  0 dB -> 10^( 0/20) = 1.00000
 * Gbuf    :  2 dB -> 10^( 2/20) = 1.25892
 * Gbuf    :  4 dB -> 10^( 4/20) = 1.58489
 * Gbuf    :  6 dB -> 10^( 6/20) = 1.99526
 * Gbuf    :  8 dB -> 10^( 8/20) = 2.51188
 * Gbuf    : 10 dB -> 10^(10/20) = 3.16227
 * Gbuf    : 12 dB -> 10^(12/20) = 3.98107
 *
 * SDM gain: 16 bits (1 sign bit & 15 bits for 32768 steps)
 * Gsdm    : 0x28/0x40
 *
 * DAC path (Vdac):
 *   dc compensation value = dc offset * 32768 / (0.85) / 1000
 *                         = dc_offset * 38.55
 * Total path (Vout):
 * (Gbuf: -2 dB)
 *   dc compensation value = dc offset * 32768 / (0.6751805) / 1000
 *                         = dc_offset * 48.53
 *
 * (Gbuf: 0 dB)
 *   dc compensation value = dc offset * 32768 / (0.85) / 1000
 *                         = dc_offset * 38.55
 *
 * (Gbuf: 2 dB)
 *   dc compensation value = dc offset * 32768 / (1.070082) / 1000
 *                         = dc_offset * 30.62
 *
 * (Gbuf: 4 dB)
 *   dc compensation value = dc offset * 32768 / (1.3471565) / 1000
 *                         = dc_offset * 24.32
 *
 * (Gbuf: 6 dB)
 *   dc compensation value = dc offset * 32768 / (1.695971) / 1000
 *                         = dc_offset * 19.32
 *
 * (Gbuf: 8 dB)
 *   dc compensation value = dc offset * 32768 / (2.135098) / 1000
 *                         = dc_offset * 15.34
 *
 * (Gbuf: 10 dB)
 *   dc compensation value = dc_offset * 32768 / (2.6879295) / 1000
 *                         = dc_offset * 12.19
 *
 * (Gbuf: 12 dB)
 *   dc compensation value = dc offset * 32768 / (3.3839095) / 1000
 *                         = dc_offset * 9.68
 */

/*
 * total path (Vout)
 * (Gbuf: -2 dB): 100 mV * 48.53
 *
 * -2dB, dB0, 2dB, 4dB, 6dB, 7dB, 10dB, 12dB
 */
static const uint32_t dc_comp_coeff_map[] = {
	4853, 3855, 3062, 2432, 1932, 1534, 1219, 968,
};

/* dac path (Vdac): 500 mV * 38.55 */
#define DC_COMP_COEFF_VDAC (0x4B4B)

/*
 * ramp step size (dc compensation value)
 * (ramping between vcm and dac stages)
 *
 * DAC path (Vdac):
 *   dc compensation value = dc offset * 38.55
 *
 * 10uF, 22uF: 50 mV * 38.55 = 0x787
 * 33uF, 47uF: 10 mV * 38.55 = 0x181
 */
static const uint16_t ramp_vcm_to_dac_step_size_map[] = {
	0x787, 0x787, 0x181, 0x181,
};

/*
 * ramp time per step for different cap value (us)
 * (ramping between vcm and dac stages)
 *
 * 10uF, 22uF: 2000
 * 33uF, 47uF:  400
 */
static const unsigned long ramp_vcm_to_dac_step_time_map[] = {
	2000, 2000, 400, 400,
};

/*
 * ramp step size (dc compensation value)
 * (ramping between op and vcm stages)
 *
 * total path (Gbuf: -2 dB): 0.08 mV * 48.53 = 4
 * total path (Gbuf:  0 dB): 0.08 mV * 38.55 = 3
 * total path (Gbuf:  2 dB): 0.08 mV * 30.62 = 2
 * total path (Gbuf:  4 dB): 0.08 mV * 24.32 = 2
 * total path (Gbuf:  6 dB): 0.08 mV * 19.32 = 2
 * total path (Gbuf:  8 dB): 0.08 mV * 15.34 = 1
 * total path (Gbuf: 10 dB): 0.08 mV * 12.19 = 1
 * total path (Gbuf: 12 dB): 0.08 mV *  9.68 = 1
 */
static const uint16_t ramp_op_to_vcm_step_size[] = {
	4, 3, 2, 2, 2, 1, 1, 1,
};

/*
 * ramp time per step for different cap value (us)
 * (ramping between op and vcm stages)
 *
 * 10uF, 22uF:  75
 * 33uF, 47uF: 125
 */
static const unsigned long ramp_op_to_vcm_step_time_map[] = {
	75, 75, 125, 125,
};

/*
 * dac stable time for different cap value (us)
 *
 * 10uF, 22uF: 20000
 * 33uF, 47uF: 40000
 */
static const unsigned long dac_stable_time_map[] = {
	20000, 20000, 40000, 40000,
};

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
		.reg = AUDIO_TOP_CON0,
		.mask = BIT(2),
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
	{ /* I2S2_OUT_MODE (44.1khz) */
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
	{ /* ADA_HPLO_TO_AUXADC */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(15),
		.val = BIT(15),
	},
	{ /* ADA_HPRO_TO_AUXADC */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(14),
		.val = BIT(14),
	},
};

static const struct reg_setting mt8167_codec_cali_cleanup_regs[] = {
	{ /* ADA_HPLO_TO_AUXADC */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(15),
		.val = 0x0,
	},
	{ /* ADA_HPRO_TO_AUXADC */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(14),
		.val = 0x0,
	},
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
		.mask = BIT(2),
		.val = BIT(2),
	},
	{
		.reg = AUDIO_TOP_CON0,
		.mask = GENMASK(26, 25),
		.val = (0x3 << 25),
	},
};

static const struct reg_setting mt8167_codec_cali_enable_vcm_to_dac_regs[] = {
	{ /* dl_rate (44.1khz) */
		.reg = ABB_AFE_CON1,
		.mask = GENMASK(3, 0),
		.val = 9,
	},
	{ /* toggle top_ctrl */
		.reg = ABB_AFE_CON11,
		.mask = ABB_AFE_CON11_TOP_CTRL,
		.val = ABB_AFE_CON11_TOP_CTRL,
	},
	{ /* toggle top_ctrl */
		.reg = ABB_AFE_CON11,
		.mask = ABB_AFE_CON11_TOP_CTRL,
		.val = 0x0,
	},
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
};

static const struct reg_setting mt8167_codec_cali_enable_dac_to_op_regs[] = {
	{
		.reg = AUDIO_CODEC_CON01,
		.mask = GENMASK(2, 0),
		.val = 0x0,
	},
	{
		.reg = AUDIO_CODEC_CON01,
		.mask = GENMASK(5, 3),
		.val = 0x0,
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
	{
		.reg = ABB_AFE_CON0,
		.mask = BIT(0),
		.val = 0x0,
	},
	{
		.reg = AUDIO_CODEC_CON01,
		.mask = GENMASK(2, 0),
		.val = 0x0,
	},
	{
		.reg = AUDIO_CODEC_CON01,
		.mask = GENMASK(5, 3),
		.val = 0x0,
	},
};

static int mt8167_codec_apply_reg_setting(struct snd_soc_codec *codec,
	const struct reg_setting regs[], uint32_t reg_nums)
{
	int i;

	for (i = 0; i < reg_nums; i++)
		snd_soc_update_bits(codec,
			regs[i].reg, regs[i].mask, regs[i].val);

	return 0;
}

#define AFIFO_SIZE (48 * 1024)
static int mt8167_codec_setup_cali_path(struct snd_soc_codec *codec,
	struct snd_dma_buffer *buf)
{
	/* enable power */
	snd_soc_update_bits(codec, AUDIO_CODEC_CON01, BIT(20), BIT(20));
	/* enable clock */
	snd_soc_update_bits(codec, AUDIO_CODEC_CON03, BIT(30), BIT(30));
	snd_soc_update_bits(codec, AUDIO_CODEC_CON04, BIT(15), BIT(15));
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
	/* disable clock */
	snd_soc_update_bits(codec, AUDIO_CODEC_CON04, BIT(15), 0x0);
	snd_soc_update_bits(codec, AUDIO_CODEC_CON03, BIT(30), 0x0);
	/* disable power */
	snd_soc_update_bits(codec, AUDIO_CODEC_CON01, BIT(20), 0x0);

	return ret;
}

static int mt8167_codec_enable_cali_path(struct snd_soc_codec *codec,
	enum hp_cali_step_id step)
{
	/* enable */
	switch (step) {
	case HP_CALI_VCM_TO_DAC:
		return mt8167_codec_apply_reg_setting(codec,
			mt8167_codec_cali_enable_vcm_to_dac_regs,
			ARRAY_SIZE(mt8167_codec_cali_enable_vcm_to_dac_regs));
	case HP_CALI_DAC_TO_OP:
		return mt8167_codec_apply_reg_setting(codec,
			mt8167_codec_cali_enable_dac_to_op_regs,
			ARRAY_SIZE(mt8167_codec_cali_enable_dac_to_op_regs));
	default:
		return 0;
	}
}

static int mt8167_codec_disable_cali_path(struct snd_soc_codec *codec)
{
	/* disable */
	return mt8167_codec_apply_reg_setting(codec,
			mt8167_codec_cali_disable_regs,
			ARRAY_SIZE(mt8167_codec_cali_disable_regs));
}

static uint32_t mt8167_codec_get_hp_cap_index(struct snd_soc_codec *codec)
{
	return (snd_soc_read(codec, AUDIO_CODEC_CON02) &
			GENMASK(20, 19)) >> 19;
}

static uint16_t mt8167_codec_ramp_vcm_to_dac_step(
	struct snd_soc_codec *codec)
{
	uint32_t index = mt8167_codec_get_hp_cap_index(codec);

	return ramp_vcm_to_dac_step_size_map[index];
}

static unsigned long mt8167_codec_ramp_vcm_to_dac_step_time(
	struct snd_soc_codec *codec)
{
	uint32_t index = mt8167_codec_get_hp_cap_index(codec);

	return ramp_vcm_to_dac_step_time_map[index];
}

static unsigned long mt8167_codec_ramp_op_to_vcm_step_time(
	struct snd_soc_codec *codec)
{
	uint32_t index = mt8167_codec_get_hp_cap_index(codec);

	return ramp_op_to_vcm_step_time_map[index];
}

static unsigned long mt8167_codec_get_dac_stable_time(
	struct snd_soc_codec *codec)
{
	uint32_t index = mt8167_codec_get_hp_cap_index(codec);

	return dac_stable_time_map[index];
}

int mt8167_codec_valid_new_dc_comp(struct snd_soc_codec *codec)
{
	/* toggle DC status */
	if (snd_soc_read(codec, ABB_AFE_CON11) &
			ABB_AFE_CON11_DC_CTRL_STATUS)
		snd_soc_update_bits(codec,
			ABB_AFE_CON11,
			ABB_AFE_CON11_DC_CTRL, 0);
	else
		snd_soc_update_bits(codec,
			ABB_AFE_CON11,
			ABB_AFE_CON11_DC_CTRL, ABB_AFE_CON11_DC_CTRL);

	return 0;
}
EXPORT_SYMBOL_GPL(mt8167_codec_valid_new_dc_comp);

void mt8167_codec_update_dc_comp(struct snd_soc_codec *codec,
	enum hp_channel_id cid,
	uint32_t lch_dccomp_val, uint32_t rch_dccomp_val)
{
	if (cid & HP_LEFT) {
		/*  L-ch DC compensation value */
		snd_soc_update_bits(codec,
			ABB_AFE_CON3, GENMASK(15, 0),
			lch_dccomp_val);
	}

	if (cid & HP_RIGHT) {
		/*  R-ch DC compensation value */
		snd_soc_update_bits(codec,
			ABB_AFE_CON4, GENMASK(15, 0),
			rch_dccomp_val);
	}

	mt8167_codec_valid_new_dc_comp(codec);
}
EXPORT_SYMBOL_GPL(mt8167_codec_update_dc_comp);

static bool mt8167_codec_ramp_one_step(struct snd_soc_codec *codec,
	int16_t dc_comp_begin, int16_t dc_comp_end,
	uint16_t step, int32_t *dc_comp_cur)
{
	if (dc_comp_begin == dc_comp_end)
		return true; /* ramping done */

	if (dc_comp_cur == NULL)
		return true;

	if (step == 0)
		return true;

	*dc_comp_cur = dc_comp_begin;

	if (*dc_comp_cur < dc_comp_end) {
		*dc_comp_cur += step;
		if (*dc_comp_cur > dc_comp_end) {
			*dc_comp_cur = dc_comp_end;
			return true;
		}
	} else {
		*dc_comp_cur -= step;
		if (*dc_comp_cur < dc_comp_end) {
			*dc_comp_cur = dc_comp_end;
			return true;
		}
	}

	return false;
}

/* ramping for dc compensation value */
void mt8167_codec_dc_offset_ramp(struct snd_soc_codec *codec,
	enum hp_cali_step_id sid, enum hp_channel_id cid,
	enum hp_sequence_id seq_id,
	uint32_t lch_dccomp[HP_CALI_NUMS][HP_PGA_GAIN_NUMS],
	uint32_t rch_dccomp[HP_CALI_NUMS][HP_PGA_GAIN_NUMS],
	uint32_t lpga_index, uint32_t rpga_index)
{
	bool hp_ramp_done = false;
	bool lch_ramp_done = false;
	bool rch_ramp_done = false;

	int32_t lch_dccomp_cur = 0;
	int32_t rch_dccomp_cur = 0;
	int32_t lch_dccomp_target = 0;
	int32_t rch_dccomp_target = 0;

	uint16_t lch_step = 0;
	uint16_t rch_step = 0;
	unsigned long step_time = 0;

	if (sid == HP_CALI_VCM_TO_DAC) {
		if (seq_id == HP_ON_SEQ) {
			/* ramp down */
			lch_dccomp_cur = lch_dccomp[sid][lpga_index];
			rch_dccomp_cur = rch_dccomp[sid][rpga_index];
			lch_dccomp_target = lch_dccomp[sid+1][lpga_index];
			rch_dccomp_target = rch_dccomp[sid+1][rpga_index];
		} else {
			/* ramp up */
			lch_dccomp_cur = lch_dccomp[sid+1][lpga_index];
			rch_dccomp_cur = rch_dccomp[sid+1][rpga_index];
			lch_dccomp_target = lch_dccomp[sid][lpga_index];
			rch_dccomp_target = rch_dccomp[sid][rpga_index];
		}
		lch_step = mt8167_codec_ramp_vcm_to_dac_step(codec);
		rch_step = mt8167_codec_ramp_vcm_to_dac_step(codec);
		step_time = mt8167_codec_ramp_vcm_to_dac_step_time(codec);
	} else if (sid == HP_CALI_OP_TO_VCM) {
		if (seq_id == HP_ON_SEQ) {
			/* ramp up */
			lch_dccomp_cur = lch_dccomp[sid-1][lpga_index];
			rch_dccomp_cur = rch_dccomp[sid-1][rpga_index];
			lch_dccomp_target = lch_dccomp[sid][lpga_index];
			rch_dccomp_target = rch_dccomp[sid][rpga_index];
		} else {
			/* ramp down */
			lch_dccomp_cur = lch_dccomp[sid][lpga_index];
			rch_dccomp_cur = rch_dccomp[sid][rpga_index];
			lch_dccomp_target = lch_dccomp[sid-1][lpga_index];
			rch_dccomp_target = rch_dccomp[sid-1][rpga_index];
		}
		lch_step = ramp_op_to_vcm_step_size[lpga_index];
		rch_step = ramp_op_to_vcm_step_size[rpga_index];
		step_time = mt8167_codec_ramp_op_to_vcm_step_time(codec);
	}

	while (!hp_ramp_done) {
		lch_ramp_done = mt8167_codec_ramp_one_step(codec,
			lch_dccomp_cur,
			lch_dccomp_target,
			lch_step, &lch_dccomp_cur);

		rch_ramp_done = mt8167_codec_ramp_one_step(codec,
			rch_dccomp_cur,
			rch_dccomp_target,
			rch_step, &rch_dccomp_cur);

		if (lch_ramp_done && rch_ramp_done)
			hp_ramp_done = true;

		mt8167_codec_update_dc_comp(codec,
			cid, lch_dccomp_cur, rch_dccomp_cur);

		usleep_range(step_time, step_time + 1);
	}
}
EXPORT_SYMBOL_GPL(mt8167_codec_dc_offset_ramp);

/* get voltage of AU_HPL/AU_HPR from AUXADC */
static int32_t mt8167_codec_query_avg_voltage(struct snd_soc_codec *codec,
	uint32_t auxadc_channel)
{
	int32_t auxadc_val_cur = 0;
	int32_t auxadc_val_min = 0;
	int32_t auxadc_val_max = 0;
	int64_t auxadc_val_sum = 0;
	int32_t auxadc_val_avg = 0;
	int32_t count = 0;
	const int32_t countlimit = 22;

	for (count = 0; count < countlimit; count++) {
#ifdef CONFIG_MTK_AUXADC
		IMM_GetOneChannelValue_Cali(auxadc_channel,
			&auxadc_val_cur);
#endif
		if (!auxadc_val_min)
			auxadc_val_min = auxadc_val_cur;

		if (auxadc_val_cur >= auxadc_val_max)
			auxadc_val_max = auxadc_val_cur;
		if (auxadc_val_cur <= auxadc_val_min)
			auxadc_val_min = auxadc_val_cur;

		auxadc_val_sum += auxadc_val_cur;

		dev_dbg(codec->dev, "%s auxadc_val_cur: %d, auxadc_val_sum: %lld\n",
			__func__, auxadc_val_cur, auxadc_val_sum);
	}

	auxadc_val_sum -= auxadc_val_max;
	auxadc_val_sum -= auxadc_val_min;

	auxadc_val_avg = auxadc_val_sum / (countlimit - 2);

	return auxadc_val_avg;
}

void mt8167_codec_show_voltage_of_hp(struct snd_soc_codec *codec)
{
	int32_t voltage = 0;

	/* enable ADA_HPLO_TO_AUXADC */
	snd_soc_update_bits(codec, AUDIO_CODEC_CON02, BIT(15), BIT(15));

	/* enable ADA_HPRO_TO_AUXADC */
	snd_soc_update_bits(codec, AUDIO_CODEC_CON02, BIT(14), BIT(14));

	voltage = mt8167_codec_query_avg_voltage(codec, AUXADC_CH_AU_HPL);
	dev_dbg(codec->dev, "%s HPL voltage %d\n", __func__, voltage);

	voltage = mt8167_codec_query_avg_voltage(codec, AUXADC_CH_AU_HPR);
	dev_dbg(codec->dev, "%s HPR voltage %d\n", __func__, voltage);

	/* disable ADA_HPLO_TO_AUXADC */
	snd_soc_update_bits(codec, AUDIO_CODEC_CON02, BIT(15), 0x0);

	/* disable ADA_HPRO_TO_AUXADC */
	snd_soc_update_bits(codec, AUDIO_CODEC_CON02, BIT(14), 0x0);
}
EXPORT_SYMBOL_GPL(mt8167_codec_show_voltage_of_hp);

static int32_t mt8167_codec_calc_dc_offset(int32_t voltage_prev,
	int32_t voltage_cur)
{
	int32_t cali_val = 0;

	cali_val = voltage_prev - voltage_cur;
	cali_val = cali_val / 100; /* 0.1 mV */

	return cali_val;
}

static uint64_t mt8167_codec_get_hp_cali_val(struct snd_soc_codec *codec,
	uint32_t auxadc_channel,
	long dc_offset[HP_CALI_NUMS][HP_PGA_GAIN_NUMS])
{
	struct snd_dma_buffer dma_buf;
	int32_t auxadc_val_target = 0;
	int32_t auxadc_val_prev = 0;
	int32_t auxadc_val_cur = 0;
	unsigned long dac_stable_time = 0;
	uint32_t pga;
#ifdef TIMESTAMP_INFO
	uint64_t stamp_1 = 0, stamp_2 = 0;
#endif
	uint64_t latency_cali_dac_to_op = 0;

	dev_dbg(codec->dev, "%s\n", __func__);

	/* enter A */
	mt8167_codec_setup_cali_path(codec, &dma_buf);

	usleep_range(50 * 1000, 60 * 1000);

	/* voltage of A */
	auxadc_val_prev =
		mt8167_codec_query_avg_voltage(codec, auxadc_channel);

	auxadc_val_target = auxadc_val_prev;

	dev_dbg(codec->dev, "%s voltage of A %d\n", __func__, auxadc_val_prev);

	/* enter B */
	mt8167_codec_enable_cali_path(codec, HP_CALI_VCM_TO_DAC);

#ifdef TIMESTAMP_INFO
	stamp_1 = sched_clock();
#endif

	dac_stable_time = mt8167_codec_get_dac_stable_time(codec);
	usleep_range(dac_stable_time, dac_stable_time + 10);

	/* voltage of B */
	auxadc_val_cur =
		mt8167_codec_query_avg_voltage(codec, auxadc_channel);
	dev_dbg(codec->dev, "%s voltage of B %d\n", __func__, auxadc_val_cur);

	/* dc_offset of A and B */
	for (pga = 0; pga < HP_PGA_GAIN_NUMS; pga++)
		dc_offset[HP_CALI_VCM_TO_DAC][pga] =
			mt8167_codec_calc_dc_offset(auxadc_val_prev,
				auxadc_val_cur);

	auxadc_val_prev = auxadc_val_cur;

#ifdef TIMESTAMP_INFO
	stamp_2 = sched_clock();

	latency_cali_dac_to_op = stamp_2 - stamp_1;
#endif

	/* enter C */
	mt8167_codec_enable_cali_path(codec, HP_CALI_DAC_TO_OP);

	usleep_range(50 * 1000, 60 * 1000);

	/*
	 * voltage after OP on varies according to PGA gain
	 * stores dc offsets per each pga gain here for runtime usage
	 * to eliminate the voltage jump before and after OP on or off
	 */
	for (pga = 0; pga < HP_PGA_GAIN_NUMS; pga++) {
		if (auxadc_channel == AUXADC_CH_AU_HPL)
			snd_soc_update_bits(codec,
				AUDIO_CODEC_CON01, GENMASK(2, 0), pga);
		else
			snd_soc_update_bits(codec,
				AUDIO_CODEC_CON01, GENMASK(5, 3), pga << 3);

		usleep_range(5, 6);

		/* voltage of C */
		auxadc_val_cur =
			mt8167_codec_query_avg_voltage(codec, auxadc_channel);

		dev_dbg(codec->dev,
			"%s voltage of C %d\n", __func__, auxadc_val_cur);

		/* dc_offset of B and C */
		dc_offset[HP_CALI_DAC_TO_OP][pga] =
			mt8167_codec_calc_dc_offset(auxadc_val_prev,
				auxadc_val_cur);

		/* dc_offset of A and C */
		dc_offset[HP_CALI_OP_TO_VCM][pga] =
			mt8167_codec_calc_dc_offset(auxadc_val_target,
				auxadc_val_cur);
	}

	mt8167_codec_disable_cali_path(codec);
	mt8167_codec_cleanup_cali_path(codec, &dma_buf);

	return latency_cali_dac_to_op;
}

static uint32_t mt8167_codec_conv_dc_offset_to_comp_val(int32_t dc_offset,
	uint32_t pga_index)
{
	uint32_t dccomp_val = 0;
	bool invert = false;

	if (!dc_offset)
		return 0;

	if (dc_offset > 0)
		invert = true;
	else
		dc_offset *= (-1);

	dccomp_val = dc_offset * dc_comp_coeff_map[pga_index] / 1000;

	/* two's complement to change the direction of dc compensation value */
	if (invert)
		dccomp_val = 0xFFFFFFFF - dccomp_val + 1;

	return dccomp_val;
}

void mt8167_codec_gather_comp_val(struct snd_soc_codec *codec,
	uint32_t dccomp_val[HP_CALI_NUMS][HP_PGA_GAIN_NUMS],
	long dc_offset[HP_CALI_NUMS][HP_PGA_GAIN_NUMS])
{
	uint32_t pga;
	int i;

	for (pga = 0; pga < HP_PGA_GAIN_NUMS; pga++)
		dccomp_val[HP_CALI_VCM_TO_DAC][pga] = DC_COMP_COEFF_VDAC;

	for (i = HP_CALI_DAC_TO_OP; i < HP_CALI_NUMS; i++) {
		for (pga = 0; pga < HP_PGA_GAIN_NUMS; pga++) {
			dccomp_val[i][pga] =
				mt8167_codec_conv_dc_offset_to_comp_val(
					dc_offset[i][pga],
					pga);
		}
	}
}
EXPORT_SYMBOL_GPL(mt8167_codec_gather_comp_val);

uint64_t mt8167_codec_get_hp_cali_comp_val(struct snd_soc_codec *codec,
	uint32_t dccomp_val[HP_CALI_NUMS][HP_PGA_GAIN_NUMS],
	long dc_offset[HP_CALI_NUMS][HP_PGA_GAIN_NUMS],
	enum hp_channel_id cid)
{
	uint32_t auxadc_channel = AUXADC_CH_AU_HPL;
	uint64_t latency_cali_dac_to_op = 0;

	if (cid == HP_LEFT)
		auxadc_channel = AUXADC_CH_AU_HPL;
	else if (cid == HP_RIGHT)
		auxadc_channel = AUXADC_CH_AU_HPR;

	latency_cali_dac_to_op =
		mt8167_codec_get_hp_cali_val(codec, auxadc_channel, dc_offset);

	mt8167_codec_gather_comp_val(codec, dccomp_val, dc_offset);

	return latency_cali_dac_to_op;
}
EXPORT_SYMBOL_GPL(mt8167_codec_get_hp_cali_comp_val);

static bool ul_is_builtin_mic(uint32_t type)
{
	bool ret = false;

	switch (type) {
	case CODEC_LOOPBACK_AMIC_TO_SPK:
	case CODEC_LOOPBACK_AMIC_TO_HP:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

static bool ul_is_headset_mic(uint32_t type)
{
	bool ret = false;

	switch (type) {
	case CODEC_LOOPBACK_HEADSET_MIC_TO_SPK:
	case CODEC_LOOPBACK_HEADSET_MIC_TO_HP:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

static bool ul_is_amic(uint32_t type)
{
	return ul_is_builtin_mic(type) ||
			ul_is_headset_mic(type);
}

static bool ul_is_dmic(uint32_t type)
{
	bool ret = false;

	switch (type) {
	case CODEC_LOOPBACK_DMIC_TO_SPK:
	case CODEC_LOOPBACK_DMIC_TO_HP:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

static bool dl_is_spk(uint32_t type)
{
	bool ret = false;

	switch (type) {
	case CODEC_LOOPBACK_AMIC_TO_SPK:
	case CODEC_LOOPBACK_DMIC_TO_SPK:
	case CODEC_LOOPBACK_HEADSET_MIC_TO_SPK:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

static bool dl_is_hp(uint32_t type)
{
	bool ret = false;

	switch (type) {
	case CODEC_LOOPBACK_AMIC_TO_HP:
	case CODEC_LOOPBACK_DMIC_TO_HP:
	case CODEC_LOOPBACK_HEADSET_MIC_TO_HP:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

static const struct reg_setting mt8167_codec_ul_rate_32000_setup_regs[] = {
	{ /* ul_rate (32khz) */
		.reg = ABB_AFE_CON1,
		.mask = GENMASK(7, 4),
		.val = 0x0,
	},
	{ /* toggle top_ctrl */
		.reg = ABB_AFE_CON11,
		.mask = ABB_AFE_CON11_TOP_CTRL,
		.val = ABB_AFE_CON11_TOP_CTRL,
	},
	{ /* toggle top_ctrl */
		.reg = ABB_AFE_CON11,
		.mask = ABB_AFE_CON11_TOP_CTRL,
		.val = 0x0,
	},
};

static const struct reg_setting mt8167_codec_ul_rate_48000_setup_regs[] = {
	{ /* ul_rate (48khz) */
		.reg = ABB_AFE_CON1,
		.mask = GENMASK(7, 4),
		.val = (0x1 << 4),
	},
	{ /* toggle top_ctrl */
		.reg = ABB_AFE_CON11,
		.mask = ABB_AFE_CON11_TOP_CTRL,
		.val = ABB_AFE_CON11_TOP_CTRL,
	},
	{ /* toggle top_ctrl */
		.reg = ABB_AFE_CON11,
		.mask = ABB_AFE_CON11_TOP_CTRL,
		.val = 0x0,
	},
};

static const struct reg_setting mt8167_codec_dl_rate_32000_setup_regs[] = {
	{ /* dl_rate (32khz) */
		.reg = ABB_AFE_CON1,
		.mask = GENMASK(3, 0),
		.val = 8,
	},
	{ /* toggle top_ctrl */
		.reg = ABB_AFE_CON11,
		.mask = ABB_AFE_CON11_TOP_CTRL,
		.val = ABB_AFE_CON11_TOP_CTRL,
	},
	{ /* toggle top_ctrl */
		.reg = ABB_AFE_CON11,
		.mask = ABB_AFE_CON11_TOP_CTRL,
		.val = 0x0,
	},
};

static const struct reg_setting mt8167_codec_dl_rate_48000_setup_regs[] = {
	{ /* dl_rate (48khz) */
		.reg = ABB_AFE_CON1,
		.mask = GENMASK(3, 0),
		.val = 10,
	},
	{ /* toggle top_ctrl */
		.reg = ABB_AFE_CON11,
		.mask = ABB_AFE_CON11_TOP_CTRL,
		.val = ABB_AFE_CON11_TOP_CTRL,
	},
	{ /* toggle top_ctrl */
		.reg = ABB_AFE_CON11,
		.mask = ABB_AFE_CON11_TOP_CTRL,
		.val = 0x0,
	},
};

static const struct reg_setting mt8167_codec_builtin_amic_enable_regs[] = {
	{ /* micbias0 */
		.reg = AUDIO_CODEC_CON03,
		.mask = BIT(17),
		.val = BIT(17),
	},
	{ /* left pga mux <- AU_VIN0 */
		.reg = AUDIO_CODEC_CON00,
		.mask = GENMASK(31, 28),
		.val = 0x0,
	},
	{ /* right pga mux <- AU_VIN2 */
		.reg = AUDIO_CODEC_CON00,
		.mask = GENMASK(13, 10),
		.val = 0x0,
	},
};

static const struct reg_setting mt8167_codec_headset_amic_enable_regs[] = {
	{ /* micbias1 */
		.reg = AUDIO_CODEC_CON03,
		.mask = BIT(6),
		.val = BIT(6),
	},
	{ /* left pga mux <- AU_VIN1 */
		.reg = AUDIO_CODEC_CON00,
		.mask = GENMASK(31, 28),
		.val = BIT(28),
	},
	{ /* right pga mux <- AU_VIN1 */
		.reg = AUDIO_CODEC_CON00,
		.mask = GENMASK(13, 10),
		.val = BIT(10),
	},
};

static const struct reg_setting mt8167_codec_amic_enable_regs[] = {
	{ /* CLKLDO power on */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(20),
		.val = BIT(20),
	},
	{ /* Audio Codec CLK on */
		.reg = AUDIO_CODEC_CON03,
		.mask = BIT(30),
		.val = BIT(30),
	},
	{ /* UL CLK on */
		.reg = AUDIO_CODEC_CON03,
		.mask = BIT(21),
		.val = BIT(21),
	},
	{ /* vcm14 */
		.reg = AUDIO_CODEC_CON00,
		.mask = AUDIO_CODEC_CON00_AUDULL_VCM14_EN,
		.val = AUDIO_CODEC_CON00_AUDULL_VCM14_EN,
	},
	{ /* vcm14 */
		.reg = AUDIO_CODEC_CON00,
		.mask = AUDIO_CODEC_CON00_AUDULR_VCM14_EN,
		.val = AUDIO_CODEC_CON00_AUDULR_VCM14_EN,
	},
	{ /* vref24 */
		.reg = AUDIO_CODEC_CON00,
		.mask = AUDIO_CODEC_CON00_AUDULL_VREF24_EN,
		.val = AUDIO_CODEC_CON00_AUDULL_VREF24_EN,
	},
	{ /* vref24 */
		.reg = AUDIO_CODEC_CON00,
		.mask = AUDIO_CODEC_CON00_AUDULR_VREF24_EN,
		.val = AUDIO_CODEC_CON00_AUDULR_VREF24_EN,
	},
	{ /* ul_en */
		.reg = ABB_AFE_CON0,
		.mask = BIT(1),
		.val = BIT(1),
	},
	{ /* left pga */
		.reg = AUDIO_CODEC_CON00,
		.mask = BIT(24),
		.val = BIT(24),
	},
	{ /* right pga */
		.reg = AUDIO_CODEC_CON00,
		.mask = BIT(6),
		.val = BIT(6),
	},
	{ /* left adc */
		.reg = AUDIO_CODEC_CON00,
		.mask = BIT(23),
		.val = BIT(23),
	},
	{ /* right adc */
		.reg = AUDIO_CODEC_CON00,
		.mask = BIT(5),
		.val = BIT(5),
	},
};

static const struct reg_setting mt8167_codec_amic_disable_regs[] = {
	{ /* left adc */
		.reg = AUDIO_CODEC_CON00,
		.mask = BIT(23),
		.val = 0x0,
	},
	{ /* right adc */
		.reg = AUDIO_CODEC_CON00,
		.mask = BIT(5),
		.val = 0x0,
	},
	{ /* left pga */
		.reg = AUDIO_CODEC_CON00,
		.mask = BIT(24),
		.val = 0x0,
	},
	{ /* right pga */
		.reg = AUDIO_CODEC_CON00,
		.mask = BIT(6),
		.val = 0x0,
	},
	{ /* micbias0 */
		.reg = AUDIO_CODEC_CON03,
		.mask = BIT(17),
		.val = 0x0,
	},
	{ /* micbias1 */
		.reg = AUDIO_CODEC_CON03,
		.mask = BIT(6),
		.val = 0x0,
	},
	{ /* left pga mux <- OPEN */
		.reg = AUDIO_CODEC_CON00,
		.mask = BIT(28),
		.val = (0x2 << 28),
	},
	{ /* right pga mux <- OPEN */
		.reg = AUDIO_CODEC_CON00,
		.mask = BIT(10),
		.val = (0x2 << 10),
	},
	{ /* ul_en */
		.reg = ABB_AFE_CON0,
		.mask = BIT(1),
		.val = 0x0,
	},
	{ /* vref24 */
		.reg = AUDIO_CODEC_CON00,
		.mask = AUDIO_CODEC_CON00_AUDULL_VREF24_EN,
		.val = 0x0,
	},
	{ /* vref24 */
		.reg = AUDIO_CODEC_CON00,
		.mask = AUDIO_CODEC_CON00_AUDULR_VREF24_EN,
		.val = 0x0,
	},
	{ /* vcm14 */
		.reg = AUDIO_CODEC_CON00,
		.mask = AUDIO_CODEC_CON00_AUDULL_VCM14_EN,
		.val = 0x0,
	},
	{ /* vcm14 */
		.reg = AUDIO_CODEC_CON00,
		.mask = AUDIO_CODEC_CON00_AUDULR_VCM14_EN,
		.val = 0x0,
	},
	{ /* UL CLK off */
		.reg = AUDIO_CODEC_CON03,
		.mask = BIT(21),
		.val = 0x0,
	},
	{ /* Audio Codec CLK off */
		.reg = AUDIO_CODEC_CON03,
		.mask = BIT(30),
		.val = 0x0,
	},
	{ /* CLKLDO power off */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(20),
		.val = 0x0,
	},
};

static const struct reg_setting mt8167_codec_dmic_enable_regs[] = {
	{ /* vcm14 */
		.reg = AUDIO_CODEC_CON00,
		.mask = AUDIO_CODEC_CON00_AUDULL_VCM14_EN,
		.val = AUDIO_CODEC_CON00_AUDULL_VCM14_EN,
	},
	{ /* vcm14 */
		.reg = AUDIO_CODEC_CON00,
		.mask = AUDIO_CODEC_CON00_AUDULR_VCM14_EN,
		.val = AUDIO_CODEC_CON00_AUDULR_VCM14_EN,
	},
	{ /* micbias0 */
		.reg = AUDIO_CODEC_CON03,
		.mask = BIT(17),
		.val = BIT(17),
	},
	{ /* ul_en */
		.reg = ABB_AFE_CON0,
		.mask = BIT(1),
		.val = BIT(1),
	},
	{ /* dig_mic_en (one-wire) */
		.reg = ABB_AFE_CON9,
		.mask = ABB_AFE_CON9_DIG_MIC_EN,
		.val = ABB_AFE_CON9_DIG_MIC_EN,
	},
	{ /* Digital microphone enable */
		.reg = AUDIO_CODEC_CON03,
		.mask = AUDIO_CODEC_CON03_DIG_MIC_EN,
		.val = AUDIO_CODEC_CON03_DIG_MIC_EN,
	},
};

static const struct reg_setting mt8167_codec_dmic_disable_regs[] = {
	{ /* Digital microphone enable */
		.reg = AUDIO_CODEC_CON03,
		.mask = AUDIO_CODEC_CON03_DIG_MIC_EN,
		.val = 0x0,
	},
	{ /* dig_mic_en (one-wire) */
		.reg = ABB_AFE_CON9,
		.mask = ABB_AFE_CON9_DIG_MIC_EN,
		.val = 0x0,
	},
	{ /* micbias0 */
		.reg = AUDIO_CODEC_CON03,
		.mask = BIT(17),
		.val = 0x0,
	},
	{ /* ul_en */
		.reg = ABB_AFE_CON0,
		.mask = BIT(1),
		.val = 0x0,
	},
	{ /* vcm14 */
		.reg = AUDIO_CODEC_CON00,
		.mask = AUDIO_CODEC_CON00_AUDULL_VCM14_EN,
		.val = 0x0,
	},
	{ /* vcm14 */
		.reg = AUDIO_CODEC_CON00,
		.mask = AUDIO_CODEC_CON00_AUDULR_VCM14_EN,
		.val = 0x0,
	},
};

static const struct reg_setting mt8167_codec_spk_enable_regs[] = {
	{ /* DL CLK on */
		.reg = AUDIO_CODEC_CON04,
		.mask = BIT(15),
		.val = BIT(15),
	},
	{ /* dl_vef24 */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(16),
		.val = BIT(16),
	},
	{ /* dac_clk */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(27),
		.val = BIT(27),
	},
	{ /* dl_vcm1 */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(13),
		.val = BIT(13),
	},
	{ /* dl_vcm2 */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(17),
		.val = BIT(17),
	},
	{ /* dl_en */
		.reg = ABB_AFE_CON0,
		.mask = BIT(0),
		.val = BIT(0),
	},
	{ /* left dac */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(15),
		.val = BIT(15),
	},
	{ /* voice amp */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(8),
		.val = BIT(8),
	},
};

static const struct reg_setting mt8167_codec_spk_disable_regs[] = {
	{ /* voice amp */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(8),
		.val = 0x0,
	},
	{ /* left dac */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(15),
		.val = 0x0,
	},
	{ /* dl_en */
		.reg = ABB_AFE_CON0,
		.mask = BIT(0),
		.val = 0x0,
	},
	{ /* dl_vcm2 */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(17),
		.val = 0x0,
	},
	{ /* dl_vcm1 */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(13),
		.val = 0x0,
	},
	{ /* dac_clk */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(27),
		.val = 0x0,
	},
	{ /* dl_vef24 */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(16),
		.val = 0x0,
	},
	{ /* DL CLK off */
		.reg = AUDIO_CODEC_CON04,
		.mask = BIT(15),
		.val = 0x0,
	},
};

static const struct reg_setting mt8167_codec_hp_enable_regs[] = {
	{ /* DL CLK on */
		.reg = AUDIO_CODEC_CON04,
		.mask = BIT(15),
		.val = BIT(15),
	},
	{ /* dl_vef24 */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(16),
		.val = BIT(16),
	},
	{ /* dac_clk */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(27),
		.val = BIT(27),
	},
	{ /* dl_vcm1 */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(13),
		.val = BIT(13),
	},
	{ /* dl_vcm2 */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(17),
		.val = BIT(17),
	},
	{ /* dl_en */
		.reg = ABB_AFE_CON0,
		.mask = BIT(0),
		.val = BIT(0),
	},
	{ /* left dac */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(15),
		.val = BIT(15),
	},
	{ /* right dac */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(14),
		.val = BIT(14),
	},
	{ /* left audio amp */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(12),
		.val = BIT(12),
	},
	{ /* right audio amp */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(11),
		.val = BIT(11),
	},
	{ /* HP Pre-charge function release */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(28),
		.val = BIT(28),
	},
	{ /* Disable the depop mux of HP drivers */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(25),
		.val = 0x0,
	},
	{ /* Disable the depop VCM gen */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(22),
		.val = 0x0,
	},
};

static const struct reg_setting mt8167_codec_hp_disable_regs[] = {
	{ /* Reset HP Pre-charge function */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(28),
		.val = 0x0,
	},
	{ /* Enable the depop mux of HP drivers */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(25),
		.val = BIT(25),
	},
	{ /* Enable depop VCM gen */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(22),
		.val = BIT(22),
	},
	{ /* left audio amp */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(12),
		.val = 0x0,
	},
	{ /* right audio amp */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(11),
		.val = 0x0,
	},
	{ /* left dac */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(15),
		.val = 0x0,
	},
	{ /* right dac */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(14),
		.val = 0x0,
	},
	{ /* dl_en */
		.reg = ABB_AFE_CON0,
		.mask = BIT(0),
		.val = 0x0,
	},
	{ /* dl_vcm2 */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(17),
		.val = 0x0,
	},
	{ /* dl_vcm1 */
		.reg = AUDIO_CODEC_CON01,
		.mask = BIT(13),
		.val = 0x0,
	},
	{ /* dac_clk */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(27),
		.val = 0x0,
	},
	{ /* dl_vef24 */
		.reg = AUDIO_CODEC_CON02,
		.mask = BIT(16),
		.val = 0x0,
	},
	{ /* DL CLK off */
		.reg = AUDIO_CODEC_CON04,
		.mask = BIT(15),
		.val = 0x0,
	},
};

static void mt8167_codec_turn_on_ul_amic_path(struct snd_soc_codec *codec,
	uint32_t lpbk_type)
{
	dev_dbg(codec->dev, "%s %s\n", __func__,
		ul_is_builtin_mic(lpbk_type) ? "built-in mic" : "headset mic");

	if (ul_is_builtin_mic(lpbk_type))
		mt8167_codec_apply_reg_setting(codec,
			mt8167_codec_builtin_amic_enable_regs,
			ARRAY_SIZE(mt8167_codec_builtin_amic_enable_regs));
	else if (ul_is_headset_mic(lpbk_type))
		mt8167_codec_apply_reg_setting(codec,
			mt8167_codec_headset_amic_enable_regs,
			ARRAY_SIZE(mt8167_codec_headset_amic_enable_regs));

	mt8167_codec_apply_reg_setting(codec,
		mt8167_codec_amic_enable_regs,
		ARRAY_SIZE(mt8167_codec_amic_enable_regs));
}

static void mt8167_codec_turn_on_ul_dmic_path(struct snd_soc_codec *codec)
{
	dev_dbg(codec->dev, "%s\n", __func__);

	mt8167_codec_apply_reg_setting(codec,
		mt8167_codec_dmic_enable_regs,
		ARRAY_SIZE(mt8167_codec_dmic_enable_regs));
}

static void mt8167_codec_turn_off_ul_amic_path(struct snd_soc_codec *codec)
{
	dev_dbg(codec->dev, "%s\n", __func__);

	mt8167_codec_apply_reg_setting(codec,
		mt8167_codec_amic_disable_regs,
		ARRAY_SIZE(mt8167_codec_amic_disable_regs));
}

static void mt8167_codec_turn_off_ul_dmic_path(struct snd_soc_codec *codec)
{
	dev_dbg(codec->dev, "%s\n", __func__);

	mt8167_codec_apply_reg_setting(codec,
		mt8167_codec_dmic_disable_regs,
		ARRAY_SIZE(mt8167_codec_dmic_disable_regs));
}

static void mt8167_codec_turn_on_dl_spk_path(struct snd_soc_codec *codec)
{
	dev_dbg(codec->dev, "%s\n", __func__);

	mt8167_codec_apply_reg_setting(codec,
		mt8167_codec_spk_enable_regs,
		ARRAY_SIZE(mt8167_codec_spk_enable_regs));
}

static void mt8167_codec_turn_on_dl_hp_path(struct snd_soc_codec *codec)
{
	dev_dbg(codec->dev, "%s\n", __func__);

	mt8167_codec_apply_reg_setting(codec,
		mt8167_codec_hp_enable_regs,
		ARRAY_SIZE(mt8167_codec_hp_enable_regs));
}

static void mt8167_codec_turn_off_dl_spk_path(struct snd_soc_codec *codec)
{
	dev_dbg(codec->dev, "%s\n", __func__);

	mt8167_codec_apply_reg_setting(codec,
		mt8167_codec_spk_disable_regs,
		ARRAY_SIZE(mt8167_codec_spk_disable_regs));
}

static void mt8167_codec_turn_off_dl_hp_path(struct snd_soc_codec *codec)
{
	dev_dbg(codec->dev, "%s\n", __func__);

	mt8167_codec_apply_reg_setting(codec,
		mt8167_codec_hp_disable_regs,
		ARRAY_SIZE(mt8167_codec_hp_disable_regs));
}

static void mt8167_codec_setup_ul_rate(struct snd_soc_codec *codec,
	uint32_t lpbk_type)
{
	if (ul_is_amic(lpbk_type))
		mt8167_codec_apply_reg_setting(codec,
			mt8167_codec_ul_rate_48000_setup_regs,
			ARRAY_SIZE(mt8167_codec_ul_rate_48000_setup_regs));
	else if (ul_is_dmic(lpbk_type))
		mt8167_codec_apply_reg_setting(codec,
			mt8167_codec_ul_rate_32000_setup_regs,
			ARRAY_SIZE(mt8167_codec_ul_rate_32000_setup_regs));
}

static void mt8167_codec_turn_on_ul_path(struct snd_soc_codec *codec,
	uint32_t lpbk_type)
{
	mt8167_codec_setup_ul_rate(codec, lpbk_type);
	if (ul_is_amic(lpbk_type))
		mt8167_codec_turn_on_ul_amic_path(codec, lpbk_type);
	else if (ul_is_dmic(lpbk_type))
		mt8167_codec_turn_on_ul_dmic_path(codec);
}

static void mt8167_codec_turn_off_ul_path(struct snd_soc_codec *codec,
	uint32_t lpbk_type)
{
	if (ul_is_amic(lpbk_type))
		mt8167_codec_turn_off_ul_amic_path(codec);
	else if (ul_is_dmic(lpbk_type))
		mt8167_codec_turn_off_ul_dmic_path(codec);
}

static void mt8167_codec_setup_dl_rate(struct snd_soc_codec *codec,
	uint32_t lpbk_type)
{
	if (ul_is_amic(lpbk_type))
		mt8167_codec_apply_reg_setting(codec,
			mt8167_codec_dl_rate_48000_setup_regs,
			ARRAY_SIZE(mt8167_codec_dl_rate_48000_setup_regs));
	else if (ul_is_dmic(lpbk_type))
		mt8167_codec_apply_reg_setting(codec,
			mt8167_codec_dl_rate_32000_setup_regs,
			ARRAY_SIZE(mt8167_codec_dl_rate_32000_setup_regs));
}

static void mt8167_codec_turn_on_dl_path(struct snd_soc_codec *codec,
	uint32_t lpbk_type)
{
	mt8167_codec_setup_dl_rate(codec, lpbk_type);
	if (dl_is_spk(lpbk_type)) {
		mt8167_codec_turn_on_dl_spk_path(codec);
#ifdef CONFIG_MTK_SPEAKER
		mt6392_int_spk_turn_on(codec);
#endif
	} else if (dl_is_hp(lpbk_type))
		mt8167_codec_turn_on_dl_hp_path(codec);
}

static void mt8167_codec_turn_off_dl_path(struct snd_soc_codec *codec,
	uint32_t lpbk_type)
{
	if (dl_is_spk(lpbk_type)) {
#ifdef CONFIG_MTK_SPEAKER
		mt6392_int_spk_turn_off(codec);
#endif
		mt8167_codec_turn_off_dl_spk_path(codec);
	} else if (dl_is_hp(lpbk_type))
		mt8167_codec_turn_off_dl_hp_path(codec);
}

void mt8167_codec_turn_on_lpbk_path(struct snd_soc_codec *codec,
	uint32_t lpbk_type)
{
	mt8167_codec_turn_on_ul_path(codec, lpbk_type);
	mt8167_codec_turn_on_dl_path(codec, lpbk_type);
}
EXPORT_SYMBOL_GPL(mt8167_codec_turn_on_lpbk_path);

void mt8167_codec_turn_off_lpbk_path(struct snd_soc_codec *codec,
	uint32_t lpbk_type)
{
	mt8167_codec_turn_off_dl_path(codec, lpbk_type);
	mt8167_codec_turn_off_ul_path(codec, lpbk_type);
}
EXPORT_SYMBOL_GPL(mt8167_codec_turn_off_lpbk_path);
