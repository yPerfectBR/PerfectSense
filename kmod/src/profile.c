// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — thermal profile mapping and set/get.
 */

#include <linux/module.h>
#include <linux/string.h>

#include "perfectsense.h"

const char *ps_thermal_profile_name(u8 tp)
{
	switch (tp) {
	case PS_TP_ECO:
		return "low-power";
	case PS_TP_QUIET:
		return "quiet";
	case PS_TP_BALANCED:
		return "balanced";
	case PS_TP_PERFORMANCE:
		return "balanced-performance";
	case PS_TP_TURBO:
		return "performance";
	default:
		return "unknown";
	}
}

int ps_thermal_profile_from_name(const char *name, u8 *tp)
{
	if (!name || !tp)
		return -EINVAL;

	if (!strcmp(name, "low-power"))
		*tp = PS_TP_ECO;
	else if (!strcmp(name, "quiet"))
		*tp = PS_TP_QUIET;
	else if (!strcmp(name, "balanced"))
		*tp = PS_TP_BALANCED;
	else if (!strcmp(name, "balanced-performance"))
		*tp = PS_TP_PERFORMANCE;
	else if (!strcmp(name, "performance"))
		*tp = PS_TP_TURBO;
	else
		return -EINVAL;

	return 0;
}

int ps_tp_to_platform(u8 tp, enum platform_profile_option *profile)
{
	if (!profile)
		return -EINVAL;

	switch (tp) {
	case PS_TP_TURBO:
		*profile = PLATFORM_PROFILE_PERFORMANCE;
		break;
	case PS_TP_PERFORMANCE:
		*profile = PLATFORM_PROFILE_BALANCED_PERFORMANCE;
		break;
	case PS_TP_BALANCED:
		*profile = PLATFORM_PROFILE_BALANCED;
		break;
	case PS_TP_QUIET:
		*profile = PLATFORM_PROFILE_QUIET;
		break;
	case PS_TP_ECO:
		*profile = PLATFORM_PROFILE_LOW_POWER;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

int ps_platform_to_tp(enum platform_profile_option profile, u8 *tp)
{
	if (!tp)
		return -EINVAL;

	switch (profile) {
	case PLATFORM_PROFILE_PERFORMANCE:
		*tp = PS_TP_TURBO;
		break;
	case PLATFORM_PROFILE_BALANCED_PERFORMANCE:
		*tp = PS_TP_PERFORMANCE;
		break;
	case PLATFORM_PROFILE_BALANCED:
		*tp = PS_TP_BALANCED;
		break;
	case PLATFORM_PROFILE_QUIET:
		*tp = PS_TP_QUIET;
		break;
	case PLATFORM_PROFILE_LOW_POWER:
		*tp = PS_TP_ECO;
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

int ps_thermal_profile_get(u8 *tp)
{
	return ps_wmi_get_misc(PS_MISC_PLATFORM_PROFILE, tp);
}

int ps_thermal_profile_set(u8 tp)
{
	bool on_ac;
	int err;

	err = ps_wmi_ac_online(&on_ac);
	if (err)
		return err;

	/*
	 * Firmware / OEM tooling only allow aggressive profiles on AC.
	 * Quiet is also blocked on battery (same policy as OEM Sense).
	 */
	if (!on_ac && (tp == PS_TP_TURBO || tp == PS_TP_PERFORMANCE ||
		       tp == PS_TP_QUIET))
		return -EOPNOTSUPP;

	err = ps_wmi_set_misc(PS_MISC_PLATFORM_PROFILE, tp);
	if (err)
		return err;

	/* Quiet/Eco: return fans to firmware auto (OEM-like behaviour) */
	if (tp == PS_TP_QUIET || tp == PS_TP_ECO)
		ps_fan_set_auto();

	ps_platform_profile_notify();
	return 0;
}
