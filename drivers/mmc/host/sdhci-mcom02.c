// SPDX-License-Identifier: GPL-2.0-only
/*
 * MCOM02 Secure Digital Host Controller Interface.
 *
 * Copyright 2017 RnD Center "ELVEES", JSC
 * Copyright 2011 - 2012 Michal Simek <monstr@monstr.eu>
 * Copyright 2012 Wind River Systems, Inc.
 * Copyright 2013 Pengutronix e.K.
 * Copyright 2013 Xilinx Inc.
 *
 * Based on sdhci-of-arasan.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/module.h>
#include <linux/of_device.h>
#include "sdhci-pltfm.h"

#define SDHCI_MCOM02_CLK_CTRL_OFFSET	0x2c

#define CLK_CTRL_TIMEOUT_SHIFT		16
#define CLK_CTRL_TIMEOUT_MASK		(0xf << CLK_CTRL_TIMEOUT_SHIFT)
#define CLK_CTRL_TIMEOUT_MIN_EXP	13

/**
 * struct sdhci_mcom02_data
 * @clk_ahb:	Pointer to the AHB clock
 */
struct sdhci_mcom02_data {
	struct clk *clk_ahb;
};

static unsigned int sdhci_mcom02_get_timeout_clock(struct sdhci_host *host)
{
	u32 div;
	unsigned long freq;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	div = readl(host->ioaddr + SDHCI_MCOM02_CLK_CTRL_OFFSET);
	div = (div & CLK_CTRL_TIMEOUT_MASK) >> CLK_CTRL_TIMEOUT_SHIFT;

	freq = clk_get_rate(pltfm_host->clk);
	freq /= 1 << (CLK_CTRL_TIMEOUT_MIN_EXP + div);

	return freq;
}

static struct sdhci_ops sdhci_mcom02_ops = {
	.set_clock = sdhci_set_clock,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.get_timeout_clock = sdhci_mcom02_get_timeout_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_reset,
	.set_uhs_signaling = sdhci_set_uhs_signaling,
};

static struct sdhci_pltfm_data sdhci_mcom02_pdata = {
	.ops = &sdhci_mcom02_ops,
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN |
		   SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN,
};

#ifdef CONFIG_PM_SLEEP
/**
 * sdhci_mcom02_suspend - Suspend method for the driver
 * @dev:	Address of the device structure
 * Returns 0 on success and error value on error
 *
 * Put the device in a low power state.
 */
static int sdhci_mcom02_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_mcom02_data *sdhci_mcom02 = pltfm_host->priv;
	int ret;

	ret = sdhci_suspend_host(host);
	if (ret)
		return ret;

	clk_disable(pltfm_host->clk);
	clk_disable(sdhci_mcom02->clk_ahb);

	return 0;
}

/**
 * sdhci_mcom02_resume - Resume method for the driver
 * @dev:	Address of the device structure
 * Returns 0 on success and error value on error
 *
 * Resume operation after suspend
 */
static int sdhci_mcom02_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_mcom02_data *sdhci_mcom02 = pltfm_host->priv;
	int ret;

	ret = clk_enable(sdhci_mcom02->clk_ahb);
	if (ret) {
		dev_err(dev, "Cannot enable AHB clock.\n");
		return ret;
	}

	ret = clk_enable(pltfm_host->clk);
	if (ret) {
		dev_err(dev, "Cannot enable SD clock.\n");
		clk_disable(sdhci_mcom02->clk_ahb);
		return ret;
	}

	return sdhci_resume_host(host);
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(sdhci_mcom02_dev_pm_ops, sdhci_mcom02_suspend,
			 sdhci_mcom02_resume);

static int sdhci_mcom02_probe(struct platform_device *pdev)
{
	int ret;
	struct clk *clk_xin;
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_mcom02_data *sdhci_mcom02;

	sdhci_mcom02 = devm_kzalloc(&pdev->dev, sizeof(*sdhci_mcom02),
				    GFP_KERNEL);
	if (!sdhci_mcom02)
		return -ENOMEM;

	sdhci_mcom02->clk_ahb = devm_clk_get(&pdev->dev, "clk_ahb");
	if (IS_ERR(sdhci_mcom02->clk_ahb)) {
		dev_err(&pdev->dev, "clk_ahb clock not found.\n");
		return PTR_ERR(sdhci_mcom02->clk_ahb);
	}

	clk_xin = devm_clk_get(&pdev->dev, "clk_xin");
	if (IS_ERR(clk_xin)) {
		dev_err(&pdev->dev, "clk_xin clock not found.\n");
		return PTR_ERR(clk_xin);
	}

	ret = clk_prepare_enable(sdhci_mcom02->clk_ahb);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable AHB clock.\n");
		return ret;
	}

	ret = clk_prepare_enable(clk_xin);
	if (ret) {
		dev_err(&pdev->dev, "Unable to enable SD clock.\n");
		goto clk_dis_ahb;
	}

	host = sdhci_pltfm_init(pdev, &sdhci_mcom02_pdata, 0);
	if (IS_ERR(host)) {
		ret = PTR_ERR(host);
		goto clk_disable_all;
	}

	sdhci_get_of_property(pdev);
	pltfm_host = sdhci_priv(host);
	pltfm_host->priv = sdhci_mcom02;
	pltfm_host->clk = clk_xin;

	ret = mmc_of_parse(host->mmc);
	if (ret) {
		dev_err(&pdev->dev, "parsing dt failed (%u)\n", ret);
		goto err_pltfm_free;
	}

	ret = sdhci_add_host(host);
	if (ret)
		goto err_pltfm_free;

	return 0;

err_pltfm_free:
	sdhci_pltfm_free(pdev);
clk_disable_all:
	clk_disable_unprepare(clk_xin);
clk_dis_ahb:
	clk_disable_unprepare(sdhci_mcom02->clk_ahb);

	return ret;
}

static int sdhci_mcom02_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_mcom02_data *sdhci_mcom02 = pltfm_host->priv;

	clk_disable_unprepare(sdhci_mcom02->clk_ahb);

	return sdhci_pltfm_unregister(pdev);
}

static const struct of_device_id sdhci_mcom02_of_match[] = {
	{ .compatible = "elvees,sdhci-mcom02" },
	{ }
};
MODULE_DEVICE_TABLE(of, sdhci_mcom02_of_match);

static struct platform_driver sdhci_mcom02_driver = {
	.driver = {
		.name = "sdhci-mcom02",
		.of_match_table = sdhci_mcom02_of_match,
		.pm = &sdhci_mcom02_dev_pm_ops,
	},
	.probe = sdhci_mcom02_probe,
	.remove = sdhci_mcom02_remove,
};

module_platform_driver(sdhci_mcom02_driver);

MODULE_DESCRIPTION("Driver for the MCom-02 SDHCI Controller");
MODULE_LICENSE("GPL");
