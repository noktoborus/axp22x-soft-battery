// SPDX-License-Identifier: GPL-2.0
/*
 * The battery of the uConsole by a model of the device tree, in place of
 * axp20x_battery (docs/plan.md of axp22x-soft-battery).
 *
 * A skeleton for the packages: it binds to the cell of the battery and
 * fails the probe. The work comes in the step of the module.
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>

/* axp20x_battery built in binds first; the blacklist cannot stop it (D36) */
#if IS_BUILTIN(CONFIG_BATTERY_AXP20X)
#error "CONFIG_BATTERY_AXP20X=y: axp20x_battery must be a module (m) or off (n)"
#endif

static int axp22x_soft_battery_probe(struct platform_device *pdev)
{
	return dev_err_probe(&pdev->dev, -ENODEV, "not written yet\n");
}

static const struct of_device_id axp22x_soft_battery_of_match[] = {
	{ .compatible = "x-powers,axp221-battery-power-supply" },
	{ }
};
MODULE_DEVICE_TABLE(of, axp22x_soft_battery_of_match);

static struct platform_driver axp22x_soft_battery_driver = {
	.probe = axp22x_soft_battery_probe,
	.driver = {
		.name = "axp22x-soft-battery",
		.of_match_table = axp22x_soft_battery_of_match,
	},
};
module_platform_driver(axp22x_soft_battery_driver);

MODULE_DESCRIPTION("AXP22x battery by a model of the device tree");
MODULE_LICENSE("GPL");
