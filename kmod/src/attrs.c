// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — Phase 5 battery / misc attributes.
 */

#include <linux/module.h>

#include "perfectsense.h"

int ps_battery_limiter_get(u8 *on)
{
	return ps_wmi_battery_health_get(PS_BAT_HEALTH_MODE, on);
}

int ps_battery_limiter_set(u8 on)
{
	if (on > 1)
		return -EINVAL;
	return ps_wmi_battery_health_set(PS_BAT_HEALTH_MODE, on);
}

int ps_battery_calibration_get(u8 *on)
{
	return ps_wmi_battery_health_get(PS_BAT_CALIBRATION_MODE, on);
}

int ps_battery_calibration_set(u8 on)
{
	if (on > 1)
		return -EINVAL;
	return ps_wmi_battery_health_set(PS_BAT_CALIBRATION_MODE, on);
}

int ps_usb_charging_get(int *minutes)
{
	u64 result;
	int err;

	if (!minutes)
		return -EINVAL;

	err = ps_wmi_apge_u64(PS_WMID_APGE_GET_FUNCTION, 0x4, &result);
	if (err)
		return err;

	switch (result) {
	case 663296ULL:
		*minutes = 0;
		break;
	case 659200ULL:
		*minutes = 10;
		break;
	case 1314560ULL:
		*minutes = 20;
		break;
	case 1969920ULL:
		*minutes = 30;
		break;
	default:
		return -ENODATA;
	}
	return 0;
}

int ps_usb_charging_set(int minutes)
{
	u64 payload;
	u64 result;

	switch (minutes) {
	case 0:
		payload = 663300ULL;
		break;
	case 10:
		payload = 659204ULL;
		break;
	case 20:
		payload = 1314564ULL;
		break;
	case 30:
		payload = 1969924ULL;
		break;
	default:
		return -EINVAL;
	}

	return ps_wmi_apge_u64(PS_WMID_APGE_SET_FUNCTION, payload, &result);
}

int ps_lcd_override_get(int *on)
{
	u64 result;
	int err;

	if (!on)
		return -EINVAL;

	err = ps_wmi_method_u64(PS_WMID_GET_GAMING_PROFILE, 0x00, &result);
	if (err)
		return err;

	/*
	 * PHN16-71 OEM Sense used exact magic values:
	 *   on  = 0x1000001000000, off = 0x1000000
	 * PHN16-72 returns a wider status word (e.g. 0xff000101ff00) where the
	 * override latch is bit 48 (0x1000000000000). That bit is also set in
	 * the classic "on" magic, so one test covers both SKUs.
	 */
	*on = (result & 0x1000000000000ULL) ? 1 : 0;
	return 0;
}

int ps_lcd_override_set(u8 on)
{
	u64 result;
	u64 payload;

	if (on > 1)
		return -EINVAL;

	payload = on ? 0x1000000000010ULL : 0x10ULL;
	return ps_wmi_method_u64(PS_WMID_SET_GAMING_PROFILE, payload, &result);
}

int ps_backlight_timeout_get(int *on)
{
	u64 result;
	int err;

	if (!on)
		return -EINVAL;

	err = ps_wmi_apge_u64(PS_WMID_APGE_GET_FUNCTION, 0x88401, &result);
	if (err)
		return err;

	/*
	 * Classic exact payloads: on=0x1E0000080000, off=0x80000.
	 * Some boots return 0 before the first set; treat that as off.
	 * Bit family 0x1E0000000000 marks "timeout enabled" across variants.
	 */
	if (result == 0x1E0000080000ULL || (result & 0x1E0000000000ULL))
		*on = 1;
	else if (result == 0x80000ULL || result == 0)
		*on = 0;
	else
		return -ENODATA;

	return 0;
}

int ps_backlight_timeout_set(u8 on)
{
	u64 result;
	u64 payload;

	if (on > 1)
		return -EINVAL;

	payload = on ? 0x1E0000088402ULL : 0x88402ULL;
	return ps_wmi_apge_u64(PS_WMID_APGE_SET_FUNCTION, payload, &result);
}

int ps_boot_animation_sound_get(u8 *on)
{
	return ps_wmi_get_misc(PS_MISC_BOOT_ANIMATION_SOUND, on);
}

int ps_boot_animation_sound_set(u8 on)
{
	if (on > 1)
		return -EINVAL;
	return ps_wmi_set_misc(PS_MISC_BOOT_ANIMATION_SOUND, on);
}
