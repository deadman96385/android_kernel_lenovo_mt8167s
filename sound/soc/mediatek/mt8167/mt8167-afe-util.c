/*
 * Mediatek audio utility
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

#include "mt8167-afe-util.h"
#include "mt8167-afe-regs.h"
#include "mt8167-afe-common.h"
#ifdef IDLE_TASK_DRIVER_API
#include "mtk_idle.h"
#define MT_CG_AUDIO_AFE (CGID(CG_AUDIO, 2))
#endif
#include <linux/device.h>

static unsigned int get_top_cg_mask(unsigned int cg_type)
{
	switch (cg_type) {
	case MT8167_AFE_CG_AFE:
		return AUD_TCON0_PDN_AFE;
	case MT8167_AFE_CG_I2S:
		return AUD_TCON0_PDN_I2S;
	case MT8167_AFE_CG_22M:
		return AUD_TCON0_PDN_22M;
	case MT8167_AFE_CG_24M:
		return AUD_TCON0_PDN_24M;
	case MT8167_AFE_CG_INTDIR_CK:
		return AUD_TCON0_PDN_INTDIR_CK;
	case MT8167_AFE_CG_APLL_TUNER:
		return AUD_TCON0_PDN_APLL_TUNER;
	case MT8167_AFE_CG_APLL2_TUNER:
		return AUD_TCON0_PDN_APLL2_TUNER;
	case MT8167_AFE_CG_HDMI:
		return AUD_TCON0_PDN_HDMI;
	case MT8167_AFE_CG_SPDIF:
		return AUD_TCON0_PDN_SPDF;
	case MT8167_AFE_CG_ADC:
		return AUD_TCON0_PDN_ADC;
	case MT8167_AFE_CG_DAC:
		return AUD_TCON0_PDN_DAC;
	case MT8167_AFE_CG_DAC_PREDIS:
		return AUD_TCON0_PDN_DAC_PREDIS;
	default:
		return 0;
	}
}


int mt8167_afe_enable_top_cg(struct mtk_afe *afe, unsigned int cg_type)
{
	unsigned int mask = get_top_cg_mask(cg_type);
	unsigned long flags;

	spin_lock_irqsave(&afe->afe_ctrl_lock, flags);

	afe->top_cg_ref_cnt[cg_type]++;
	if (afe->top_cg_ref_cnt[cg_type] == 1)
		regmap_update_bits(afe->regmap, AUDIO_TOP_CON0, mask, 0x0);

	spin_unlock_irqrestore(&afe->afe_ctrl_lock, flags);

	return 0;
}

int mt8167_afe_disable_top_cg(struct mtk_afe *afe, unsigned cg_type)
{
	unsigned int mask = get_top_cg_mask(cg_type);
	unsigned long flags;

	spin_lock_irqsave(&afe->afe_ctrl_lock, flags);

	afe->top_cg_ref_cnt[cg_type]--;
	if (afe->top_cg_ref_cnt[cg_type] == 0)
		regmap_update_bits(afe->regmap, AUDIO_TOP_CON0, mask, mask);
	else if (afe->top_cg_ref_cnt[cg_type] < 0)
		afe->top_cg_ref_cnt[cg_type] = 0;

	spin_unlock_irqrestore(&afe->afe_ctrl_lock, flags);

	return 0;
}

int mt8167_afe_enable_main_clk(struct mtk_afe *afe)
{
#if defined(COMMON_CLOCK_FRAMEWORK_API)
	int ret;

	ret = clk_prepare_enable(afe->clocks[MT8167_CLK_TOP_PDN_AUD]);
	if (ret)
		goto err;
#endif

	mt8167_afe_enable_top_cg(afe, MT8167_AFE_CG_AFE);
	return 0;

#if defined(COMMON_CLOCK_FRAMEWORK_API)
err:
	dev_err(afe->dev, "%s failed %d\n", __func__, ret);
	return ret;
#endif
}

int mt8167_afe_disable_main_clk(struct mtk_afe *afe)
{
	mt8167_afe_disable_top_cg(afe, MT8167_AFE_CG_AFE);
#if defined(COMMON_CLOCK_FRAMEWORK_API)
	clk_disable_unprepare(afe->clocks[MT8167_CLK_TOP_PDN_AUD]);
#endif
	return 0;
}

int mt8167_afe_emi_clk_on(struct mtk_afe *afe)
{
#ifdef IDLE_TASK_DRIVER_API
	mutex_lock(&afe->emi_clk_mutex);
	if (afe->emi_clk_ref_cnt == 0) {
		disable_dpidle_by_bit(MT_CG_AUDIO_AFE);
		disable_soidle_by_bit(MT_CG_AUDIO_AFE);
	}
	afe->emi_clk_ref_cnt++;
	mutex_unlock(&afe->emi_clk_mutex);
#endif
	return 0;
}

int mt8167_afe_emi_clk_off(struct mtk_afe *afe)
{
#ifdef IDLE_TASK_DRIVER_API
	mutex_lock(&afe->emi_clk_mutex);
	afe->emi_clk_ref_cnt--;
	if (afe->emi_clk_ref_cnt == 0) {
		enable_dpidle_by_bit(MT_CG_AUDIO_AFE);
		enable_soidle_by_bit(MT_CG_AUDIO_AFE);
	} else if (afe->emi_clk_ref_cnt < 0) {
		afe->emi_clk_ref_cnt = 0;
	}
	mutex_unlock(&afe->emi_clk_mutex);
#endif
	return 0;
}

int mt8167_afe_enable_afe_on(struct mtk_afe *afe)
{
	unsigned long flags;

	spin_lock_irqsave(&afe->afe_ctrl_lock, flags);

	afe->afe_on_ref_cnt++;
	if (afe->afe_on_ref_cnt == 1)
		regmap_update_bits(afe->regmap, AFE_DAC_CON0, 0x1, 0x1);

	spin_unlock_irqrestore(&afe->afe_ctrl_lock, flags);

	return 0;
}

int mt8167_afe_disable_afe_on(struct mtk_afe *afe)
{
	unsigned long flags;

	spin_lock_irqsave(&afe->afe_ctrl_lock, flags);

	afe->afe_on_ref_cnt--;
	if (afe->afe_on_ref_cnt == 0)
		regmap_update_bits(afe->regmap, AFE_DAC_CON0, 0x1, 0x0);
	else if (afe->afe_on_ref_cnt < 0)
		afe->afe_on_ref_cnt = 0;

	spin_unlock_irqrestore(&afe->afe_ctrl_lock, flags);

	return 0;
}

int mt8167_afe_enable_apll_tuner_cfg(struct mtk_afe *afe, unsigned int apll)
{
	mutex_lock(&afe->afe_clk_mutex);

	afe->apll_tuner_ref_cnt[apll]++;
	if (afe->apll_tuner_ref_cnt[apll] != 1) {
		mutex_unlock(&afe->afe_clk_mutex);
		return 0;
	}

	if (apll == MT8167_AFE_APLL1) {
		regmap_update_bits(afe->regmap, AFE_APLL1_TUNER_CFG,
				   AFE_APLL1_TUNER_CFG_MASK, 0x832);
		regmap_update_bits(afe->regmap, AFE_APLL1_TUNER_CFG,
				   AFE_APLL1_TUNER_CFG_EN_MASK, 0x1);
	} else {
		regmap_update_bits(afe->regmap, AFE_APLL2_TUNER_CFG,
				   AFE_APLL2_TUNER_CFG_MASK, 0x634);
		regmap_update_bits(afe->regmap, AFE_APLL2_TUNER_CFG,
				   AFE_APLL2_TUNER_CFG_EN_MASK, 0x1);
	}

	mutex_unlock(&afe->afe_clk_mutex);
	return 0;
}

int mt8167_afe_disable_apll_tuner_cfg(struct mtk_afe *afe, unsigned int apll)
{
	mutex_lock(&afe->afe_clk_mutex);

	afe->apll_tuner_ref_cnt[apll]--;
	if (afe->apll_tuner_ref_cnt[apll] == 0) {
		if (apll == MT8167_AFE_APLL1)
			regmap_update_bits(afe->regmap, AFE_APLL1_TUNER_CFG,
					   AFE_APLL1_TUNER_CFG_EN_MASK, 0x0);
		else
			regmap_update_bits(afe->regmap, AFE_APLL2_TUNER_CFG,
					   AFE_APLL2_TUNER_CFG_EN_MASK, 0x0);
	} else if (afe->apll_tuner_ref_cnt[apll] < 0) {
		afe->apll_tuner_ref_cnt[apll] = 0;
	}

	mutex_unlock(&afe->afe_clk_mutex);
	return 0;
}

int mt8167_afe_enable_apll_associated_cfg(struct mtk_afe *afe, unsigned int apll)
{
	if (apll == MT8167_AFE_APLL1) {
		clk_prepare_enable(afe->clocks[MT8167_CLK_ENGEN1]);
		mt8167_afe_enable_top_cg(afe, MT8167_AFE_CG_22M);
#ifdef ENABLE_AFE_APLL_TUNER
		mt8167_afe_enable_top_cg(afe, MT8167_AFE_CG_APLL_TUNER);
		mt8167_afe_enable_apll_tuner_cfg(afe, MT8167_AFE_APLL1);
#endif
	} else {
		clk_prepare_enable(afe->clocks[MT8167_CLK_ENGEN2]);
		mt8167_afe_enable_top_cg(afe, MT8167_AFE_CG_24M);
#ifdef ENABLE_AFE_APLL_TUNER
		mt8167_afe_enable_top_cg(afe, MT8167_AFE_CG_APLL2_TUNER);
		mt8167_afe_enable_apll_tuner_cfg(afe, MT8167_AFE_APLL2);
#endif
	}

	return 0;
}

int mt8167_afe_disable_apll_associated_cfg(struct mtk_afe *afe, unsigned int apll)
{
	if (apll == MT8167_AFE_APLL1) {
#ifdef ENABLE_AFE_APLL_TUNER
		mt8167_afe_disable_apll_tuner_cfg(afe, MT8167_AFE_APLL1);
		mt8167_afe_disable_top_cg(afe, MT8167_AFE_CG_APLL_TUNER);
#endif
		mt8167_afe_disable_top_cg(afe, MT8167_AFE_CG_22M);
		clk_disable_unprepare(afe->clocks[MT8167_CLK_ENGEN1]);
	} else {
#ifdef ENABLE_AFE_APLL_TUNER
		mt8167_afe_disable_apll_tuner_cfg(afe, MT8167_AFE_APLL2);
		mt8167_afe_disable_top_cg(afe, MT8167_AFE_CG_APLL2_TUNER);
#endif
		mt8167_afe_disable_top_cg(afe, MT8167_AFE_CG_24M);
		clk_disable_unprepare(afe->clocks[MT8167_CLK_ENGEN2]);
	}

	return 0;
}

int mt8167_afe_enable_irq_by_mode(struct mtk_afe *afe, unsigned int mode)
{
	unsigned long flags;

	spin_lock_irqsave(&afe->afe_ctrl_lock, flags);

	afe->irq_mode_ref_cnt[mode]++;
	if (afe->irq_mode_ref_cnt[mode] > 1) {
		spin_unlock_irqrestore(&afe->afe_ctrl_lock, flags);
		return 0;
	}

	switch (mode) {
	case MT8167_AFE_IRQ_1:
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON, 1 << 0, 1 << 0);
		break;
	case MT8167_AFE_IRQ_2:
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON, 1 << 1, 1 << 1);
		break;
	case MT8167_AFE_IRQ_5:
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON2, 1 << 3, 1 << 3);
		break;
	case MT8167_AFE_IRQ_7:
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON, 1 << 14, 1 << 14);
		break;
	case MT8167_AFE_IRQ_9:
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON2, 1 << 2, 1 << 2);
		break;
	case MT8167_AFE_IRQ_10:
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON2, 1 << 4, 1 << 4);
		break;
	case MT8167_AFE_IRQ_11:
		regmap_update_bits(afe->regmap, AFE_TSF_CON,
		    AFE_TSF_CON_HDMIOUT_AUTO_EN |
		    AFE_TSF_CON_HDMIOUT_HW_DIS |
		    AFE_TSF_CON_DL2_AUTO_EN |
		    AFE_TSF_CON_DL1_AUTO_EN |
		    AFE_TSF_CON_DL_HW_DIS,
		    AFE_TSF_CON_HDMIOUT_HW_DIS |
		    AFE_TSF_CON_DL_HW_DIS);
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON2, 1 << 5, 1 << 5);
		break;
	default:
		break;
	}

	spin_unlock_irqrestore(&afe->afe_ctrl_lock, flags);

	return 0;
}

int mt8167_afe_disable_irq_by_mode(struct mtk_afe *afe, unsigned int mode)
{
	unsigned long flags;

	spin_lock_irqsave(&afe->afe_ctrl_lock, flags);

	afe->irq_mode_ref_cnt[mode]--;
	if (afe->irq_mode_ref_cnt[mode] > 0) {
		spin_unlock_irqrestore(&afe->afe_ctrl_lock, flags);
		return 0;
	} else if (afe->irq_mode_ref_cnt[mode] < 0) {
		afe->irq_mode_ref_cnt[mode] = 0;
		spin_unlock_irqrestore(&afe->afe_ctrl_lock, flags);
		return 0;
	}

	switch (mode) {
	case MT8167_AFE_IRQ_1:
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON, 1 << 0, 0 << 0);
		regmap_write(afe->regmap, AFE_IRQ_CLR, 1 << 0);
		break;
	case MT8167_AFE_IRQ_2:
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON, 1 << 1, 0 << 1);
		regmap_write(afe->regmap, AFE_IRQ_CLR, 1 << 1);
		break;
	case MT8167_AFE_IRQ_5:
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON2, 1 << 3, 0 << 3);
		regmap_write(afe->regmap, AFE_IRQ_CLR, 1 << 4);
		break;
	case MT8167_AFE_IRQ_7:
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON, 1 << 14, 0 << 14);
		regmap_write(afe->regmap, AFE_IRQ_CLR, 1 << 6);
		break;
	case MT8167_AFE_IRQ_9:
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON2, 1 << 2, 0 << 2);
		regmap_write(afe->regmap, AFE_IRQ_CLR, 1 << 8);
		break;
	case MT8167_AFE_IRQ_10:
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON2, 1 << 4, 0 << 4);
		regmap_write(afe->regmap, AFE_IRQ_CLR, 1 << 9);
		break;
	case MT8167_AFE_IRQ_11:
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON2, 1 << 5, 0 << 5);
		regmap_write(afe->regmap, AFE_IRQ_CLR, 1 << 10);
		break;
	default:
		break;
	}

	spin_unlock_irqrestore(&afe->afe_ctrl_lock, flags);

	return 0;
}

int mt8167_afe_enable_spdif_in_clk(struct mtk_afe *afe)
{
	int ret = 0;

#ifdef COMMON_CLOCK_FRAMEWORK_API
	ret = clk_prepare_enable(afe->clocks[MT8167_CLK_SPDIFIN_SEL]);
	if (ret)
		pr_err("%s clk_prepare_enable %s fail %d\n",
		       __func__, "spdifin_sel", ret);

	ret = clk_set_parent(afe->clocks[MT8167_CLK_SPDIFIN_SEL],
			     afe->clocks[MT8167_CLK_TOP_UNIVPLL_D2]);
	if (ret)
		pr_err("%s clk_set_parent %s-%s fail %d\n",
		       __func__, "spdifin_sel", "univpll_div2", ret);

	ret = clk_prepare_enable(afe->clocks[MT8167_CLK_SPDIF_IN]);
	if (ret)
		pr_err("%s clk_prepare_enable %s fail %d\n",
		       __func__, "spdif_in", ret);
#endif

	return ret;
}

int mt8167_afe_disable_spdif_in_clk(struct mtk_afe *afe)
{
#ifdef COMMON_CLOCK_FRAMEWORK_API
	clk_disable_unprepare(afe->clocks[MT8167_CLK_SPDIF_IN]);
	clk_disable_unprepare(afe->clocks[MT8167_CLK_SPDIFIN_SEL]);
#endif
	return 0;
}

int mt8167_afe_enable_spdif_in_detect(struct mtk_afe *afe)
{
	unsigned int port_sel;

	if (afe->spdif_in_state.detect_en)
		return 0;

	switch (afe->spdif_in_state.port) {
	case SPDIF_IN_PORT_OPT:
		port_sel = AFE_SPDIFIN_INT_EXT_SEL_OPTICAL;
		break;
	case SPDIF_IN_PORT_COAXIAL:
		port_sel = AFE_SPDIFIN_INT_EXT_SEL_COAXIAL;
		break;
	case SPDIF_IN_PORT_ARC:
		port_sel = AFE_SPDIFIN_INT_EXT_SEL_ARC;
		break;
	case SPDIF_IN_PORT_NONE:
	default:
		dev_err(afe->dev, "%s unexpected port %u\n", __func__,
			afe->spdif_in_state.port);
		return 0;
	}

	mt8167_afe_enable_main_clk(afe);

	mt8167_afe_enable_spdif_in_clk(afe);

	mt8167_afe_enable_top_cg(afe, MT8167_AFE_CG_INTDIR_CK);

	mt8167_afe_enable_afe_on(afe);

	regmap_write(afe->regmap, SPDIFIN_FREQ_INFO, 0x00877986);
	regmap_write(afe->regmap, SPDIFIN_FREQ_INFO_2, 0x006596e8);
	regmap_write(afe->regmap, SPDIFIN_FREQ_INFO_3, 0x000005a5);
	regmap_write(afe->regmap, AFE_SPDIFIN_BR, 0x00039000);

	regmap_update_bits(afe->regmap,
			   AFE_SPDIFIN_INT_EXT2,
			   SPDIFIN_594MODE_MASK,
			   SPDIFIN_594MODE_EN);

	regmap_update_bits(afe->regmap,
			   AFE_SPDIFIN_CFG1,
			   AFE_SPDIFIN_CFG1_SET_MASK,
			   AFE_SPDIFIN_CFG1_INT_BITS |
			   AFE_SPDIFIN_CFG1_SEL_BCK_SPDIFIN |
			   AFE_SPDIFIN_CFG1_FIFOSTART_5POINTS);

	regmap_update_bits(afe->regmap,
			   AFE_SPDIFIN_INT_EXT,
			   AFE_SPDIFIN_INT_EXT_SET_MASK,
			   AFE_SPDIFIN_INT_EXT_DATALAT_ERR_EN |
			   port_sel);

	regmap_update_bits(afe->regmap,
			   AFE_SPDIFIN_CFG0,
			   AFE_SPDIFIN_CFG0_SET_MASK,
			   AFE_SPDIFIN_CFG0_GMAT_BC_256_CYCLES |
			   AFE_SPDIFIN_CFG0_INT_EN |
			   AFE_SPDIFIN_CFG0_DE_CNT(4) |
			   AFE_SPDIFIN_CFG0_DE_SEL_CNT |
			   AFE_SPDIFIN_CFG0_MAX_LEN_NUM(237));

	mt8167_afe_enable_irq_by_mode(afe, MT8167_AFE_IRQ_9);

	regmap_update_bits(afe->regmap,
			   AFE_SPDIFIN_CFG0,
			   AFE_SPDIFIN_CFG0_EN,
			   AFE_SPDIFIN_CFG0_EN);

	afe->spdif_in_state.detect_en = true;

	return 0;
}

int mt8167_afe_disable_spdif_in_detect(struct mtk_afe *afe)
{
	if (!afe->spdif_in_state.detect_en)
		return 0;

	regmap_update_bits(afe->regmap,
			   AFE_SPDIFIN_CFG0,
			   AFE_SPDIFIN_CFG0_EN,
			   0x0);

	mt8167_afe_disable_irq_by_mode(afe, MT8167_AFE_IRQ_9);

	mt8167_afe_disable_afe_on(afe);

	mt8167_afe_disable_top_cg(afe, MT8167_AFE_CG_INTDIR_CK);

	mt8167_afe_disable_spdif_in_clk(afe);

	mt8167_afe_disable_main_clk(afe);

	afe->spdif_in_state.detect_en = false;

	return 0;
}

static unsigned int get_spdif_in_clear_bits(unsigned int v)
{
	unsigned int bits = 0;

	if (v & AFE_SPDIFIN_DEBUG3_PRE_ERR_NON_STS)
		bits |= AFE_SPDIFIN_EC_PRE_ERR_CLEAR;
	if (v & AFE_SPDIFIN_DEBUG3_PRE_ERR_B_STS)
		bits |= AFE_SPDIFIN_EC_PRE_ERR_B_CLEAR;
	if (v & AFE_SPDIFIN_DEBUG3_PRE_ERR_M_STS)
		bits |= AFE_SPDIFIN_EC_PRE_ERR_M_CLEAR;
	if (v & AFE_SPDIFIN_DEBUG3_PRE_ERR_W_STS)
		bits |= AFE_SPDIFIN_EC_PRE_ERR_W_CLEAR;
	if (v & AFE_SPDIFIN_DEBUG3_PRE_ERR_BITCNT_STS)
		bits |= AFE_SPDIFIN_EC_PRE_ERR_BITCNT_CLEAR;
	if (v & AFE_SPDIFIN_DEBUG3_PRE_ERR_PARITY_STS)
		bits |= AFE_SPDIFIN_EC_PRE_ERR_PARITY_CLEAR;
	if (v & AFE_SPDIFIN_DEBUG3_TIMEOUT_ERR_STS)
		bits |= AFE_SPDIFIN_EC_TIMEOUT_INT_CLEAR;
	if (v & AFE_SPDIFIN_INT_EXT2_LRCK_CHANGE)
		bits |= AFE_SPDIFIN_EC_DATA_LRCK_CHANGE_CLEAR;
	if (v & AFE_SPDIFIN_DEBUG1_DATALAT_ERR)
		bits |= AFE_SPDIFIN_EC_DATA_LATCH_CLEAR;
	if (v & AFE_SPDIFIN_DEBUG3_CHSTS_PREAMPHASIS_STS)
		bits |= AFE_SPDIFIN_EC_CHSTS_PREAMPHASIS_CLEAR;
	if (v & AFE_SPDIFIN_DEBUG2_FIFO_ERR)
		bits |= AFE_SPDIFIN_EC_FIFO_ERR_CLEAR;
	if (v & AFE_SPDIFIN_DEBUG2_CHSTS_INT_FLAG)
		bits |= AFE_SPDIFIN_EC_CHSTS_COLLECTION_CLEAR;
	if (v & AFE_SPDIFIN_DEBUG2_PERR_9TIMES_FLAG)
		bits |= AFE_SPDIFIN_EC_USECODE_COLLECTION_CLEAR;

	return bits;
}

static unsigned int get_spdif_in_sample_rate(struct mtk_afe *afe)
{
	unsigned int val, fs;

	regmap_read(afe->regmap, AFE_SPDIFIN_INT_EXT2, &val);

	val &= AFE_SPDIFIN_INT_EXT2_ROUGH_FS_MASK;

	switch (val) {
	case AFE_SPDIFIN_INT_EXT2_FS_32K:
		fs = 32000;
		break;
	case AFE_SPDIFIN_INT_EXT2_FS_44D1K:
		fs = 44100;
		break;
	case AFE_SPDIFIN_INT_EXT2_FS_48K:
		fs = 48000;
		break;
	case AFE_SPDIFIN_INT_EXT2_FS_88D2K:
		fs = 88200;
		break;
	case AFE_SPDIFIN_INT_EXT2_FS_96K:
		fs = 96000;
		break;
	case AFE_SPDIFIN_INT_EXT2_FS_176D4K:
		fs = 176400;
		break;
	case AFE_SPDIFIN_INT_EXT2_FS_192K:
		fs = 192000;
		break;
	default:
		fs = 0;
		break;
	}

	return fs;
}

static void spdif_in_reset_and_clear_error(struct mtk_afe *afe,
					   unsigned int err_bits)
{
	regmap_update_bits(afe->regmap,
			   AFE_SPDIFIN_CFG0,
			   AFE_SPDIFIN_CFG0_INT_EN |
			   AFE_SPDIFIN_CFG0_EN,
			   0x0);

	regmap_write(afe->regmap,
		     AFE_SPDIFIN_EC,
		     err_bits);

	regmap_update_bits(afe->regmap,
			   AFE_SPDIFIN_CFG0,
			   AFE_SPDIFIN_CFG0_INT_EN |
			   AFE_SPDIFIN_CFG0_EN,
			   AFE_SPDIFIN_CFG0_INT_EN |
			   AFE_SPDIFIN_CFG0_EN);
}

int mt8167_afe_spdif_in_isr(struct mtk_afe *afe)
{
	unsigned int debug1, debug2, debug3, int_ext2;
	unsigned int err;
	unsigned int rate;

	regmap_read(afe->regmap, AFE_SPDIFIN_INT_EXT2, &int_ext2);
	regmap_read(afe->regmap, AFE_SPDIFIN_DEBUG1, &debug1);
	regmap_read(afe->regmap, AFE_SPDIFIN_DEBUG2, &debug2);
	regmap_read(afe->regmap, AFE_SPDIFIN_DEBUG3, &debug3);

	err = (int_ext2 & AFE_SPDIFIN_INT_EXT2_LRCK_CHANGE) |
	      (debug1 & AFE_SPDIFIN_DEBUG1_DATALAT_ERR) |
	      (debug2 & AFE_SPDIFIN_DEBUG2_FIFO_ERR) |
	      (debug3 & AFE_SPDIFIN_DEBUG3_ALL_ERR);

	dev_dbg(afe->dev,
		"%s int_ext2(0x%x) debug(0x%x,0x%x,0x%x) err(0x%x)\n",
		__func__, int_ext2, debug1, debug2, debug3, err);

	if (err != 0) {
		if (afe->spdif_in_state.rate > 0)
			afe->spdif_in_state.rate = 0;

		spdif_in_reset_and_clear_error(afe,
			get_spdif_in_clear_bits(err));

		return 0;
	}

	rate = get_spdif_in_sample_rate(afe);
	if (rate == 0) {
		spdif_in_reset_and_clear_error(afe, AFE_SPDIFIN_EC_CLEAR_ALL);
		return 0;
	}

	afe->spdif_in_state.rate = rate;

	/* clear all interrupt bits */
	regmap_write(afe->regmap,
		     AFE_SPDIFIN_EC,
		     AFE_SPDIFIN_EC_CLEAR_ALL);

	return 0;
}

void mt8167_afe_check_and_reset_spdif_in(struct mtk_afe *afe)
{
	unsigned int i;
	unsigned int debug1;

	for (i = 0; i < 6; i++) {
		if (afe->spdif_in_state.ch_status[i] != 0xffffffff)
			return;
	}

	regmap_read(afe->regmap, AFE_SPDIFIN_DEBUG1, &debug1);

	if (((debug1 & AFE_SPDIFIN_DEBUG1_CS_MASK) >> 24) != 0x10)
		return;

	spdif_in_reset_and_clear_error(afe, AFE_SPDIFIN_EC_CLEAR_ALL);
}

