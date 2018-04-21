/*
 * mtk-afe-contols.h  --  Mediatek Platform driver ALSA contorls
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

#ifndef _MT8167_AFE_CONTROLS_H_
#define _MT8167_AFE_CONTROLS_H_

struct snd_soc_platform;
struct mtk_afe;

int mt8167_afe_add_controls(struct snd_soc_platform *platform);
void mt8167_afe_set_hw_digital_gain(struct mtk_afe *afe, unsigned int gain, int gain_type);

#endif
