/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PERFECTSENSE_H
#define PERFECTSENSE_H

#include <linux/device.h>
#include <linux/platform_profile.h>
#include <linux/types.h>

#define PS_API_VERSION		4

/* Acer WMI GUIDs */
#define PS_WMID_GUID3		"61EF69EA-865C-4BC3-A502-A0DEBA0CB531" /* ApgeAction */
#define PS_WMID_GUID4		"7A4DDFE7-5B5D-40B4-8595-4408E0CC7F56" /* Gaming */
#define PS_WMID_GUID5		"79772EC5-04B1-4bfd-843C-61E7F77B6CC9" /* Battery health */

/* GUID4 gaming methods */
#define PS_WMID_SET_GAMING_PROFILE	1
#define PS_WMID_GET_GAMING_PROFILE	3
#define PS_WMID_GET_SYS_INFO		5
#define PS_WMID_SET_FAN_BEHAVIOR	14
#define PS_WMID_SET_FAN_SPEED		16
#define PS_WMID_GET_FAN_SPEED		17
#define PS_WMID_SET_MISC		22
#define PS_WMID_GET_MISC		23

/* GUID3 ApgeAction methods */
#define PS_WMID_APGE_SET_FUNCTION	1
#define PS_WMID_APGE_GET_FUNCTION	2

/* GUID5 battery health methods */
#define PS_WMID_GET_BATTERY_HEALTH	20
#define PS_WMID_SET_BATTERY_HEALTH	21

#define PS_CMD_BAT_STATUS	0x02

#define PS_MISC_BOOT_ANIMATION_SOUND	0x06
#define PS_MISC_SUPPORTED_PROFILES	0x000A
#define PS_MISC_PLATFORM_PROFILE	0x000B

#define PS_MISC_INDEX_MASK	GENMASK_ULL(7, 0)
#define PS_MISC_VALUE_MASK	GENMASK_ULL(15, 8)
#define PS_MISC_STATUS_MASK	GENMASK_ULL(7, 0)

#define PS_FAN_ID_CPU		0x01
#define PS_FAN_ID_GPU		0x04
#define PS_FAN_ID_MASK		GENMASK_ULL(7, 0)
#define PS_FAN_VALUE_MASK	GENMASK_ULL(15, 8)
#define PS_FAN_STATUS_MASK	GENMASK_ULL(7, 0)

#define PS_BAT_HEALTH_MODE		1
#define PS_BAT_CALIBRATION_MODE		2

/* Firmware thermal profile codes (Predator V4) */
enum ps_thermal_profile {
	PS_TP_QUIET		= 0x00,
	PS_TP_BALANCED		= 0x01,
	PS_TP_PERFORMANCE	= 0x04,
	PS_TP_TURBO		= 0x05,
	PS_TP_ECO		= 0x06,
};

int ps_wmi_method_u64(u32 method_id, u64 in, u64 *out);
int ps_wmi_method_u32_u64(u32 method_id, u32 in, u64 *out);
int ps_wmi_apge_u64(u32 method_id, u64 in, u64 *out);
int ps_wmi_get_misc(u8 index, u8 *value);
int ps_wmi_set_misc(u8 index, u8 value);
int ps_wmi_ac_online(bool *online);
int ps_wmi_battery_health_get(u8 mode, u8 *enabled);
int ps_wmi_battery_health_set(u8 mode, u8 enabled);

int ps_thermal_profile_get(u8 *tp);
int ps_thermal_profile_set(u8 tp);
int ps_thermal_profile_from_name(const char *name, u8 *tp);
const char *ps_thermal_profile_name(u8 tp);

int ps_tp_to_platform(u8 tp, enum platform_profile_option *profile);
int ps_platform_to_tp(enum platform_profile_option profile, u8 *tp);

int ps_fan_get_duty(u8 *cpu_duty, u8 *gpu_duty);
int ps_fan_set(u8 cpu_pct, u8 gpu_pct);
int ps_fan_set_auto(void);
void ps_fan_get_target(u8 *cpu_pct, u8 *gpu_pct);

/* Phase 5 attrs: returns 0/1 (or usb minutes). Unknown → -ENODATA mapped in sysfs as -1 */
int ps_battery_limiter_get(u8 *on);
int ps_battery_limiter_set(u8 on);
int ps_battery_calibration_get(u8 *on);
int ps_battery_calibration_set(u8 on);
int ps_usb_charging_get(int *minutes);
int ps_usb_charging_set(int minutes);
int ps_lcd_override_get(int *on);
int ps_lcd_override_set(u8 on);
int ps_backlight_timeout_get(int *on);
int ps_backlight_timeout_set(u8 on);
int ps_boot_animation_sound_get(u8 *on);
int ps_boot_animation_sound_set(u8 on);

int ps_sysfs_register(struct device *dev);
void ps_sysfs_unregister(struct device *dev);

int ps_platform_profile_register(struct device *parent);
void ps_platform_profile_unregister(void);
void ps_platform_profile_notify(void);

bool ps_dmi_supported(void);

#endif /* PERFECTSENSE_H */
