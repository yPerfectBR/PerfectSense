/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PERFECTSENSE_H
#define PERFECTSENSE_H

#include <linux/types.h>

#define PS_API_VERSION		1

/* Acer gaming WMI GUID (Predator V4 class) */
#define PS_WMID_GUID4		"7A4DDFE7-5B5D-40B4-8595-4408E0CC7F56"

#define PS_WMID_SET_MISC	22
#define PS_WMID_GET_MISC	23

#define PS_MISC_SUPPORTED_PROFILES	0x000A
#define PS_MISC_PLATFORM_PROFILE	0x000B

#define PS_MISC_INDEX_MASK	GENMASK_ULL(7, 0)
#define PS_MISC_VALUE_MASK	GENMASK_ULL(15, 8)
#define PS_MISC_STATUS_MASK	GENMASK_ULL(7, 0)

/* Firmware thermal profile codes (Predator V4) */
enum ps_thermal_profile {
	PS_TP_QUIET		= 0x00,
	PS_TP_BALANCED		= 0x01,
	PS_TP_PERFORMANCE	= 0x04,
	PS_TP_TURBO		= 0x05,
	PS_TP_ECO		= 0x06,
};

int ps_wmi_get_misc(u8 index, u8 *value);
int ps_thermal_profile_get(u8 *tp);
const char *ps_thermal_profile_name(u8 tp);

int ps_sysfs_register(struct device *dev);
void ps_sysfs_unregister(struct device *dev);

bool ps_dmi_supported(void);

#endif /* PERFECTSENSE_H */
