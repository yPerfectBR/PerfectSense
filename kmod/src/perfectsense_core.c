// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — core init (Phase 1: probe + profile get).
 *
 * Unload conflicting Acer gaming WMI modules before loading this driver.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/wmi.h>

#include "perfectsense.h"

static struct platform_device *ps_pdev;

static int __init perfectsense_init(void)
{
	u8 tp;
	int err;

	if (!ps_dmi_supported()) {
		pr_err("unsupported DMI (expected PHN16-72 class)\n");
		return -ENODEV;
	}

	if (!wmi_has_guid(PS_WMID_GUID4)) {
		pr_err("gaming WMI GUID not present\n");
		return -ENODEV;
	}

	/* Sanity: firmware must answer a misc get */
	err = ps_thermal_profile_get(&tp);
	if (err) {
		pr_err("thermal profile get failed: %d\n", err);
		return err;
	}

	ps_pdev = platform_device_register_simple("perfectsense", PLATFORM_DEVID_NONE,
						  NULL, 0);
	if (IS_ERR(ps_pdev)) {
		pr_err("platform device register failed\n");
		return PTR_ERR(ps_pdev);
	}

	err = ps_sysfs_register(&ps_pdev->dev);
	if (err) {
		pr_err("sysfs register failed: %d\n", err);
		platform_device_unregister(ps_pdev);
		ps_pdev = NULL;
		return err;
	}

	pr_info("loaded (api_version=%u, thermal_profile=%s / 0x%02x)\n",
		PS_API_VERSION, ps_thermal_profile_name(tp), tp);
	return 0;
}

static void __exit perfectsense_exit(void)
{
	if (ps_pdev) {
		ps_sysfs_unregister(&ps_pdev->dev);
		platform_device_unregister(ps_pdev);
		ps_pdev = NULL;
	}
	pr_info("unloaded\n");
}

module_init(perfectsense_init);
module_exit(perfectsense_exit);

MODULE_AUTHOR("PerfectSense authors");
MODULE_DESCRIPTION("PerfectSense WMI driver (PHN16-72)");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.0");
MODULE_ALIAS("dmi:*:svnAcer*:pn*PHN16-72*:");
