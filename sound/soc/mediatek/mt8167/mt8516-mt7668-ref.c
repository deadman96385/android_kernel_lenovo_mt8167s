/*
 * mt8516-mt7668-ref.c  --  MT8516_MT7668 REF ALSA SoC machine driver
 *
 * Copyright (c) 2017 MediaTek Inc.
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

#include <linux/module.h>
#include <sound/soc.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <sound/pcm_params.h>


#define ENUM_TO_STR(enum) #enum

enum PINCTRL_PIN_STATE {
	PIN_STATE_DEFAULT = 0,
	PIN_STATE_MAX
};

struct mt8516_mt7668_ref_priv {
	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_states[PIN_STATE_MAX];
};

static const char * const mt8516_mt7668_ref_pinctrl_pin_str[PIN_STATE_MAX] = {
	"default",
};

/* Digital audio interface glue - connects codec <---> CPU */
static struct snd_soc_dai_link mt8516_mt7668_ref_dais[] = {
	/* Front End DAI links */
	{
		.name = "I2S 8CH Playback",
		.stream_name = "I2S8CH Playback",
		.cpu_dai_name = "HDMI",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	{
		.name = "TDM Capture",
		.stream_name = "TDM_Capture",
		.cpu_dai_name = "TDM_IN",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "VUL Capture",
		.stream_name = "MultiMedia1_Capture",
		.cpu_dai_name = "VUL",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "AWB Capture",
		.stream_name = "DL1_AWB_Record",
		.cpu_dai_name = "AWB",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "DL1 Playback",
		.stream_name = "MultiMedia1_PLayback",
		.cpu_dai_name = "DL1",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	{
		.name = "DAI Capture",
		.stream_name = "VOIP_Call_BT_Capture",
		.cpu_dai_name = "DAI",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "DL2 Playback",
		.stream_name = "MultiMedia2_PLayback",
		.cpu_dai_name = "DL2",
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	/* Backend End DAI links */
	{
		.name = "HDMI BE",
		.cpu_dai_name = "HDMIO",
		.no_pcm = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.dpcm_playback = 1,
	},
	{
		.name = "2ND EXT Codec",
		.cpu_dai_name = "2ND I2S",
		.no_pcm = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "DMIC BE",
		.cpu_dai_name = "INT ADDA",
		.no_pcm = 1,
		.codec_name = "mt8167-codec",
		.codec_dai_name = "mt8167-codec-dai",
		.dpcm_capture = 1,
	},
	{
		.name = "TDM IN BE",
		.cpu_dai_name = "TDM_IN_IO",
		.no_pcm = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_IB_IF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.dpcm_capture = 1,
	},
	{
		.name = "EXT Codec",
		.cpu_dai_name = "I2S",
		.no_pcm = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		.name = "MRG BT BE",
		.cpu_dai_name = "MRG BT",
		.no_pcm = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
};

static const struct snd_soc_dapm_widget mt8516_mt7668_ref_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("External Line In1"),
	SND_SOC_DAPM_INPUT("External Line In2"),
	SND_SOC_DAPM_OUTPUT("Virtual SPK Out1"),
	SND_SOC_DAPM_OUTPUT("Virtual SPK Out2"),
};

static const struct snd_soc_dapm_route mt8516_mt7668_ref_audio_map[] = {
	{"I2S Capture", NULL, "External Line In1"},
	{"2ND I2S Capture", NULL, "External Line In2"},
	{"Virtual SPK Out1", NULL, "I2S Playback"},
	{"Virtual SPK Out2", NULL, "2ND I2S Playback"},

	/* ADDA clock - Uplink */
	{"AIF TX", NULL, "AFE_CLK"},
	{"AIF TX", NULL, "AD_CLK"},
};

static int mt8516_mt7668_ref_suspend_post(struct snd_soc_card *card)
{
	return 0;
}

static int mt8516_mt7668_ref_resume_pre(struct snd_soc_card *card)
{
	return 0;
}

static struct snd_soc_card mt8516_mt7668_ref_card = {
	.name = "mt-snd-card",
	.owner = THIS_MODULE,
	.dai_link = mt8516_mt7668_ref_dais,
	.num_links = ARRAY_SIZE(mt8516_mt7668_ref_dais),
	.dapm_widgets = mt8516_mt7668_ref_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt8516_mt7668_ref_dapm_widgets),
	.dapm_routes = mt8516_mt7668_ref_audio_map,
	.num_dapm_routes = ARRAY_SIZE(mt8516_mt7668_ref_audio_map),
	.suspend_post = mt8516_mt7668_ref_suspend_post,
	.resume_pre = mt8516_mt7668_ref_resume_pre,
};

static int mt8516_mt7668_ref_gpio_probe(struct snd_soc_card *card)
{
	struct mt8516_mt7668_ref_priv *card_data;
	int ret = 0;
	int i;

	card_data = snd_soc_card_get_drvdata(card);

	card_data->pinctrl = devm_pinctrl_get(card->dev);
	if (IS_ERR(card_data->pinctrl)) {
		ret = PTR_ERR(card_data->pinctrl);
		dev_err(card->dev, "%s pinctrl_get failed %d\n",
			__func__, ret);
		goto exit;
	}

	for (i = 0 ; i < PIN_STATE_MAX ; i++) {
		card_data->pin_states[i] =
			pinctrl_lookup_state(card_data->pinctrl,
				mt8516_mt7668_ref_pinctrl_pin_str[i]);
		if (IS_ERR(card_data->pin_states[i])) {
			ret = PTR_ERR(card_data->pin_states[i]);
			dev_warn(card->dev,
				"%s Can't find pinctrl state %s %d\n",
				__func__,
				mt8516_mt7668_ref_pinctrl_pin_str[i],
				ret);
		}
	}

	/* default state */
	if (!IS_ERR(card_data->pin_states[PIN_STATE_DEFAULT])) {
		ret = pinctrl_select_state(card_data->pinctrl,
				card_data->pin_states[PIN_STATE_DEFAULT]);
		if (ret) {
			dev_err(card->dev, "%s failed to select state %d\n",
				__func__, ret);
			goto exit;
		}
	}

exit:
	return ret;
}

static int mt8516_mt7668_ref_dev_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &mt8516_mt7668_ref_card;
	struct device_node *platform_node;
	int ret, i;
	struct mt8516_mt7668_ref_priv *card_data;

	platform_node = of_parse_phandle(pdev->dev.of_node,
					 "mediatek,platform", 0);
	if (!platform_node) {
		dev_err(&pdev->dev, "Property 'platform' missing or invalid\n");
		return -EINVAL;
	}

	for (i = 0; i < card->num_links; i++) {
		if (mt8516_mt7668_ref_dais[i].platform_name)
			continue;
		mt8516_mt7668_ref_dais[i].platform_of_node = platform_node;
	}

	card->dev = &pdev->dev;

	card_data = devm_kzalloc(&pdev->dev,
		sizeof(struct mt8516_mt7668_ref_priv), GFP_KERNEL);
	if (!card_data) {
		ret = -ENOMEM;
		dev_err(&pdev->dev,
			"%s allocate card private data fail %d\n",
			__func__, ret);
		return ret;
	}

	snd_soc_card_set_drvdata(card, card_data);

	mt8516_mt7668_ref_gpio_probe(card);

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(&pdev->dev, "%s snd_soc_register_card fail %d\n",
		__func__, ret);
		return ret;
	}

	return ret;
}

static const struct of_device_id mt8516_mt7668_ref_dt_match[] = {
	{ .compatible = "mediatek,mt8516-mt7668-ref", },
	{ }
};
MODULE_DEVICE_TABLE(of, mt8516_mt7668_ref_dt_match);

static struct platform_driver mt8516_mt7668_ref_mach_driver = {
	.driver = {
		   .name = "mt8516-mt7668-ref",
		   .of_match_table = mt8516_mt7668_ref_dt_match,
#ifdef CONFIG_PM
		   .pm = &snd_soc_pm_ops,
#endif
	},
	.probe = mt8516_mt7668_ref_dev_probe,
};

module_platform_driver(mt8516_mt7668_ref_mach_driver);

/* Module information */
MODULE_DESCRIPTION("MT8516_MT7668 REF ALSA SoC machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mt8516-mt7668-ref");

