// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — core init (Phase 10: hotkey / AC WMI events).
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
	bool on_ac;
	int err;

	if (!ps_dmi_supported()) {
		pr_err("unsupported DMI (expected PHN16-72 class)\n");
		return -ENODEV;
	}

	if (!wmi_has_guid(PS_WMID_GUID4)) {
		pr_err("gaming WMI GUID not present\n");
		return -ENODEV;
	}

	err = ps_thermal_profile_get(&tp);
	if (err) {
		pr_err("thermal profile get failed: %d\n", err);
		return err;
	}

	err = ps_wmi_ac_online(&on_ac);
	if (err)
		pr_warn("AC status read failed: %d\n", err);

	ps_pdev = platform_device_register_simple("perfectsense", PLATFORM_DEVID_NONE,
						  NULL, 0);
	if (IS_ERR(ps_pdev)) {
		pr_err("platform device register failed\n");
		return PTR_ERR(ps_pdev);
	}

	err = ps_sysfs_register(&ps_pdev->dev);
	if (err) {
		pr_err("sysfs register failed: %d\n", err);
		goto err_pdev;
	}

	err = ps_platform_profile_register(&ps_pdev->dev);
	if (err) {
		pr_err("platform_profile register failed: %d\n", err);
		goto err_sysfs;
	}

	err = ps_events_register(&ps_pdev->dev);
	if (err) {
		pr_err("events register failed: %d\n", err);
		goto err_pp;
	}

	err = ps_hwmon_register(&ps_pdev->dev);
	if (err) {
		pr_err("hwmon register failed: %d\n", err);
		goto err_events;
	}

	/* Start from firmware auto unless userspace overrides */
	ps_fan_set_auto();

	{
		u8 cpu_d = 0, gpu_d = 0;

		if (!ps_fan_get_duty(&cpu_d, &gpu_d))
			pr_info("loaded (api_version=%u, thermal_profile=%s / 0x%02x, ac=%d, fan_duty=%u,%u)\n",
				PS_API_VERSION, ps_thermal_profile_name(tp), tp,
				on_ac ? 1 : 0, cpu_d, gpu_d);
		else
			pr_info("loaded (api_version=%u, thermal_profile=%s / 0x%02x, ac=%d)\n",
				PS_API_VERSION, ps_thermal_profile_name(tp), tp,
				on_ac ? 1 : 0);
	}
	return 0;

err_events:
	ps_events_unregister();
err_pp:
	ps_platform_profile_unregister();
err_sysfs:
	ps_sysfs_unregister(&ps_pdev->dev);
err_pdev:
	platform_device_unregister(ps_pdev);
	ps_pdev = NULL;
	return err;
}

static void __exit perfectsense_exit(void)
{
	/* Tear down userspace-facing interfaces before event/input state. */
	ps_fan_set_auto();
	ps_hwmon_unregister();
	if (ps_pdev) {
		ps_sysfs_unregister(&ps_pdev->dev);
		ps_events_unregister();
		ps_platform_profile_unregister();
		platform_device_unregister(ps_pdev);
		ps_pdev = NULL;
	} else {
		ps_events_unregister();
		ps_platform_profile_unregister();
	}
	pr_info("unloaded\n");
}

module_init(perfectsense_init);
module_exit(perfectsense_exit);

MODULE_AUTHOR("PerfectSense authors");
MODULE_DESCRIPTION("PerfectSense WMI driver (PHN16-72)");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.0.16");
MODULE_ALIAS("dmi:*:svnAcer*:pn*PHN16-72*:");
MODULE_ALIAS("wmi:676AA15E-6A47-4D9F-A2CC-1E6D18D14026");
