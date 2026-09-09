// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — ACPI platform_profile class registration.
 */

#include <linux/module.h>
#include <linux/platform_profile.h>

#include "perfectsense.h"

static struct device *ps_pp_dev;

static int ps_pp_probe(void *drvdata, unsigned long *choices)
{
	u8 supported = 0;
	int err;

	err = ps_wmi_get_misc(PS_MISC_SUPPORTED_PROFILES, &supported);
	if (err)
		return err;

	if (supported & BIT(PS_TP_ECO))
		set_bit(PLATFORM_PROFILE_LOW_POWER, choices);
	if (supported & BIT(PS_TP_QUIET))
		set_bit(PLATFORM_PROFILE_QUIET, choices);
	if (supported & BIT(PS_TP_BALANCED))
		set_bit(PLATFORM_PROFILE_BALANCED, choices);
	if (supported & BIT(PS_TP_PERFORMANCE))
		set_bit(PLATFORM_PROFILE_BALANCED_PERFORMANCE, choices);
	if (supported & BIT(PS_TP_TURBO))
		set_bit(PLATFORM_PROFILE_PERFORMANCE, choices);

	if (*choices == 0) {
		set_bit(PLATFORM_PROFILE_LOW_POWER, choices);
		set_bit(PLATFORM_PROFILE_QUIET, choices);
		set_bit(PLATFORM_PROFILE_BALANCED, choices);
		set_bit(PLATFORM_PROFILE_BALANCED_PERFORMANCE, choices);
		set_bit(PLATFORM_PROFILE_PERFORMANCE, choices);
	}

	return 0;
}

static int ps_pp_get(struct device *dev, enum platform_profile_option *profile)
{
	u8 tp;
	int err;

	err = ps_thermal_profile_get(&tp);
	if (err)
		return err;

	return ps_tp_to_platform(tp, profile);
}

static int ps_pp_set(struct device *dev, enum platform_profile_option profile)
{
	u8 tp;
	int err;

	err = ps_platform_to_tp(profile, &tp);
	if (err)
		return err;

	/*
	 * Call WMI set directly to avoid notify recursion from
	 * ps_thermal_profile_set → platform_profile_notify while already in
	 * a class store path.
	 */
	{
		bool on_ac;

		err = ps_wmi_ac_online(&on_ac);
		if (err)
			return err;
		if (!on_ac && (tp == PS_TP_TURBO || tp == PS_TP_PERFORMANCE ||
			       tp == PS_TP_QUIET))
			return -EOPNOTSUPP;

		err = ps_wmi_set_misc(PS_MISC_PLATFORM_PROFILE, tp);
		if (err)
			return err;

		if (tp == PS_TP_QUIET || tp == PS_TP_ECO)
			ps_fan_set_auto();

		return 0;
	}
}

static const struct platform_profile_ops ps_pp_ops = {
	.probe = ps_pp_probe,
	.profile_get = ps_pp_get,
	.profile_set = ps_pp_set,
};

int ps_platform_profile_register(struct device *parent)
{
	ps_pp_dev = platform_profile_register(parent, "perfectsense", NULL,
					      &ps_pp_ops);
	if (IS_ERR(ps_pp_dev)) {
		int err = PTR_ERR(ps_pp_dev);

		ps_pp_dev = NULL;
		return err;
	}

	return 0;
}

void ps_platform_profile_unregister(void)
{
	if (ps_pp_dev) {
		platform_profile_remove(ps_pp_dev);
		ps_pp_dev = NULL;
	}
}

void ps_platform_profile_notify(void)
{
	if (ps_pp_dev)
		platform_profile_notify(ps_pp_dev);
}
