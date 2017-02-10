/*
 * mt8516_p1.c  --  MT8516P1 ALSA SoC machine driver
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

#include <linux/module.h>
#include <sound/soc.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>

enum PINCTRL_PIN_STATE {
	PIN_STATE_DEFAULT = 0,
	PIN_STATE_MAX
};

struct mt8516_p1_priv {
	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_states[PIN_STATE_MAX];
	int tdmadc_rst_gpio;
	struct regulator *extamp_supply;
	struct regulator *tdmadc_supply;
	struct regulator *amp_rst_supply;
};

static const char * const mt8516_p1_pinctrl_pin_str[PIN_STATE_MAX] = {
	"default",
};

static int master_volume_info(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 100;
	return 0;
}

static int master_volume_get(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = 100;
	return 0;
}

static int master_volume_put(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int master_switch_info(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int master_switch_get(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = 1;
	return 0;
}

static int master_switch_put(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int pcm_state_info(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 10;
	return 0;
}

static int pcm_state_get(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int pcm_state_put(struct snd_kcontrol *kctl,
	struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static const struct snd_kcontrol_new mt8516_p1_soc_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Volume",
		.index = 0,
		.info = master_volume_info,
		.get = master_volume_get,
		.put = master_volume_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Switch",
		.index = 0,
		.info = master_switch_info,
		.get = master_switch_get,
		.put = master_switch_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "PCM State",
		.index = 0,
		.info = pcm_state_info,
		.get = pcm_state_get,
		.put = pcm_state_put,
	},
};

/* Digital audio interface glue - connects codec <---> CPU */
static struct snd_soc_dai_link mt8516_p1_dais[] = {
	/* Front End DAI links */
	{
		.name = "I2S 8CH Playback",
		.stream_name = "I2S8CH Playback",
		.cpu_dai_name = "HDMI",
		.codec_dai_name = "cs4382a-i2s",
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.dynamic = 1,
		.dpcm_playback = 1,
	},
	{
		.name = "HDMI BE",
		.cpu_dai_name = "HDMIO",
		.no_pcm = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
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
		.name = "Line In Capture",
		.stream_name = "Line_In_Capture",
		.cpu_dai_name = "2ND I2S",
		.no_pcm = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.dpcm_capture = 1,
	},
	{
		.name = "DMIC Capture",
		.stream_name = "DMIC_Capture",
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
		.dai_fmt = SND_SOC_DAIFMT_LEFT_J | SND_SOC_DAIFMT_IB_IF |
			   SND_SOC_DAIFMT_CBS_CFS,
		.dpcm_capture = 1,
	},
};

static struct snd_soc_card mt8516_p1_card = {
	.name = "mt-snd-card",
	.owner = THIS_MODULE,
	.dai_link = mt8516_p1_dais,
	.num_links = ARRAY_SIZE(mt8516_p1_dais),
	.controls = mt8516_p1_soc_controls,
	.num_controls = ARRAY_SIZE(mt8516_p1_soc_controls),
};

static int mt8516_p1_gpio_probe(struct snd_soc_card *card)
{
	struct mt8516_p1_priv *card_data;
	struct device_node *np = card->dev->of_node;
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
				mt8516_p1_pinctrl_pin_str[i]);
		if (IS_ERR(card_data->pin_states[i])) {
			ret = PTR_ERR(card_data->pin_states[i]);
			dev_warn(card->dev, "%s Can't find pinctrl state %s %d\n",
				__func__, mt8516_p1_pinctrl_pin_str[i], ret);
		}
	}

	card_data->tdmadc_rst_gpio = of_get_named_gpio(np, "tdmadc-rst-gpio", 0);
	if (!gpio_is_valid(card_data->tdmadc_rst_gpio))
		dev_warn(card->dev, "%s get invalid tdmadc_rst_gpio %d\n",
				__func__, card_data->tdmadc_rst_gpio);

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

	/* default enable cs4382a power */
	if (gpio_is_valid(card_data->tdmadc_rst_gpio)) {
		ret = devm_gpio_request_one(card->dev, card_data->tdmadc_rst_gpio, GPIOF_OUT_INIT_LOW, "tdm adc reset");
		if (ret < 0)
			dev_err(card->dev, "%s failed to init tdm adc reset gpio %d\n",
				__func__, ret);
		gpio_set_value(card_data->tdmadc_rst_gpio, 1);
	}

exit:
	return ret;
}

static int mt8516_p1_regulator_probe(struct snd_soc_card *card)
{
	struct mt8516_p1_priv *card_data;
	int isenable, vol, ret;

	card_data = snd_soc_card_get_drvdata(card);

	card_data->extamp_supply =  devm_regulator_get(card->dev, "extamp");
	if (IS_ERR(card_data->extamp_supply)) {
		ret = PTR_ERR(card_data->extamp_supply);
		dev_err(card->dev, "%s failed to get ext amp regulator %d\n",
				__func__, ret);
		return ret;
	}
	ret = regulator_enable(card_data->extamp_supply);
	if (ret != 0) {
		dev_err(card->dev, "%s failed to enable ext amp supply %d\n",
			__func__, ret);
		return ret;
	}

	card_data->tdmadc_supply =  devm_regulator_get(card->dev, "tdmadc");
	if (IS_ERR(card_data->tdmadc_supply)) {
		ret = PTR_ERR(card_data->tdmadc_supply);
		dev_err(card->dev, "%s failed to get tdm adc regulator %d\n",
				__func__, ret);
		return ret;
	}
	ret = regulator_enable(card_data->tdmadc_supply);
	if (ret != 0) {
		dev_err(card->dev, "%s failed to enable tdm adc supply %d\n",
			__func__, ret);
		return ret;
	}

	card_data->amp_rst_supply =  devm_regulator_get(card->dev, "cs4382a-rst");
	if (IS_ERR(card_data->amp_rst_supply)) {
		ret = PTR_ERR(card_data->amp_rst_supply);
		dev_err(card->dev, "%s failed to get cs4382a-rst regulator %d\n",
				__func__, ret);
		return ret;
	}
	ret = regulator_set_voltage(card_data->amp_rst_supply, 3300000, 3300000);
	if (ret != 0) {
		dev_err(card->dev, "%s failed to set reset supply to 3.3v %d\n",
			__func__, ret);
		return ret;
	}
	ret = regulator_enable(card_data->amp_rst_supply);
	if (ret != 0) {
		dev_err(card->dev, "%s failed to enable reset supply %d\n",
			__func__, ret);
		return ret;
	}
	isenable = regulator_is_enabled(card_data->amp_rst_supply);
	if (isenable != 1) {
		dev_err(card->dev, "%s cs4382 reset supply is not enabled\n",
				__func__);
	}
	vol = regulator_get_voltage(card_data->amp_rst_supply);
	if (vol != 3300000)
		dev_err(card->dev, "%s cs4382 reset supply != 3.3v (%d)\n",
				__func__, ret);

	return 0;
}

static int mt8516_p1_dev_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &mt8516_p1_card;
	struct device_node *platform_node;
	struct device_node *codec_node;
	int ret, i;
	struct mt8516_p1_priv *card_data;

	platform_node = of_parse_phandle(pdev->dev.of_node,
					 "mediatek,platform", 0);
	if (!platform_node) {
		dev_err(&pdev->dev, "Property 'platform' missing or invalid\n");
		return -EINVAL;
	}

	codec_node = of_parse_phandle(pdev->dev.of_node,
					 "mediatek,audio-codec", 0);
	if (!codec_node) {
		dev_err(&pdev->dev, "Property 'platform' missing or invalid\n");
		return -EINVAL;
	}

	for (i = 0; i < card->num_links; i++) {
		if (mt8516_p1_dais[i].platform_name)
			continue;
		mt8516_p1_dais[i].platform_of_node = platform_node;
	}

	for (i = 0; i < card->num_links; i++) {
		if (mt8516_p1_dais[i].codec_name)
			continue;
		mt8516_p1_dais[i].codec_of_node = codec_node;
	}

	card->dev = &pdev->dev;

	card_data = devm_kzalloc(&pdev->dev,
		sizeof(struct mt8516_p1_priv), GFP_KERNEL);

	if (!card_data) {
		ret = -ENOMEM;
		dev_err(&pdev->dev,
			"%s allocate card private data fail %d\n",
			__func__, ret);
		return ret;
	}

	snd_soc_card_set_drvdata(card, card_data);

	mt8516_p1_regulator_probe(card);
	mt8516_p1_gpio_probe(card);

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "%s snd_soc_register_card fail %d\n",
			__func__, ret);

	return ret;
}

static const struct of_device_id mt8516_p1_dt_match[] = {
	{ .compatible = "mediatek,mt8516-soc-p1", },
	{ }
};
MODULE_DEVICE_TABLE(of, mt8516_p1_dt_match);

static struct platform_driver mt8516_p1_mach_driver = {
	.driver = {
		   .name = "mt8516-soc-p1",
		   .of_match_table = mt8516_p1_dt_match,
#ifdef CONFIG_PM
		   .pm = &snd_soc_pm_ops,
#endif
	},
	.probe = mt8516_p1_dev_probe,
};

module_platform_driver(mt8516_p1_mach_driver);

/* Module information */
MODULE_DESCRIPTION("MT8516P1 ALSA SoC machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:mt8516-p1");

