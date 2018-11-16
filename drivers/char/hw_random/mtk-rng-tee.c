/*
 * Driver for Mediatek Hardware Random Number Generator
 *
 * Copyright (C) 2017 Greta Zhang <greta.zhang@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#define MTK_RNG_DEV KBUILD_MODNAME

#include <linux/err.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "mt-plat/trustzone/kree/system.h"
#include "mt-plat/trustzone/tz_cross/ta_trng.h"

#define to_mtk_rng(p)	container_of(p, struct mtk_rng, rng)

struct mtk_rng {
	struct hwrng rng;
};

static int mtk_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct mtk_rng *priv = to_mtk_rng(rng);
	int retval = 0;
	#define PARAM_LEN 4
	KREE_SESSION_HANDLE trng_session;
	union MTEEC_PARAM param[PARAM_LEN];
	TZ_RESULT ret;
	int i, j, remain_rng_len, loop, cpy_len;

	if (max < 4) {
		dev_info((struct device *)priv->rng.priv, "Error: data size < 4 bytes\n");
		return -EINVAL;
	} else {
		remain_rng_len = max/4;  /* how many TRNG_DATA is needed */
	}

	//dev_info((struct device *)priv->rng.priv, "DEBUG: rng_mtee_read\n");
	ret = KREE_CreateSession(TZ_TA_TRNG_UUID, &trng_session);
	if (ret != TZ_RESULT_SUCCESS) {
		dev_info((struct device *)priv->rng.priv, "Error: KREE_CreateSession failed(0x%x)\n", ret);
		return -ENODEV;
	}

	loop = (remain_rng_len + (PARAM_LEN - 1)) / PARAM_LEN;
	for (i = 0; i < loop; i++) { /* once read 4 TRNG_DATA */
		ret = KREE_TeeServiceCall(trng_session, TZCMD_TRNG_RD,
			TZ_ParamTypes4(TZPT_VALUE_INOUT, TZPT_VALUE_INOUT,
			TZPT_VALUE_INOUT, TZPT_VALUE_INOUT),
			param);
		if (ret != TZ_RESULT_SUCCESS) {
			dev_info((struct device *)priv->rng.priv, "Error: KREE_TeeServiceCall failed(0x%x)\n", ret);
			return -ENODEV;
		}

		cpy_len = min(4, remain_rng_len); //for example: max is 14, remain_rng_len is 3
		for (j = 0; j < cpy_len; j++)
			*(u32 *)(buf + j * sizeof(u32)) = param[j].value.a;

		/*
		dev_info((struct device *)priv->rng.priv, "DEBUG: rng_mtee_read:[0]0x%x, [1]0x%x, [2]0x%x, [3]0x%x\n",
			param[0].value.a, param[1].value.a, param[2].value.a, param[3].value.a);
		*/

		retval += cpy_len * sizeof(u32);
		buf += cpy_len * sizeof(u32);
		remain_rng_len -= cpy_len;
	}

	ret = KREE_CloseSession(trng_session);
	if (ret != TZ_RESULT_SUCCESS) {
		dev_info((struct device *)priv->rng.priv, "Error: KREE_CloseSession failed(0x%x)\n", ret);
		return -ENODEV;
	}

	return retval || !wait ? retval : -EIO;
}

static int mtk_rng_probe(struct platform_device *pdev)
{
	int ret;
	struct mtk_rng *priv;
	KREE_SESSION_HANDLE trng_session;

	/* check if TA supports REE service */
	ret = KREE_CreateSession(TZ_TA_TRNG_UUID, &trng_session);
	if (ret != TZ_RESULT_SUCCESS)
		return -ENODEV;
	else
		KREE_CloseSession(trng_session);

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->rng.name = pdev->name;
	priv->rng.read = mtk_rng_read;
	priv->rng.priv = (unsigned long)&pdev->dev;
	priv->rng.quality = 100; /* per mill: set to 100 to dominate 10% of /dev/random */

	ret = devm_hwrng_register(&pdev->dev, &priv->rng);
	if (ret) {
		dev_err(&pdev->dev, "failed to register rng device: %d\n",
			ret);
		return ret;
	}

	dev_info(&pdev->dev, "registered RNG driver\n");

	return 0;
}

static const struct of_device_id mtk_rng_match[] = {
	{ .compatible = "mediatek,mt8167-tee-rng" },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_rng_match);

static struct platform_driver mtk_rng_driver = {
	.probe          = mtk_rng_probe,
	.driver = {
		.name = MTK_RNG_DEV,
		.of_match_table = mtk_rng_match,
	},
};

module_platform_driver(mtk_rng_driver);

MODULE_DESCRIPTION("Mediatek Random Number Generator Driver");
MODULE_AUTHOR("Greta Zhang <greta.zhang@mediatek.com>");
MODULE_LICENSE("GPL");

