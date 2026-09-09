// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — sysfs group under perfect_sense/
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/sysfs.h>

#include "perfectsense.h"

static ssize_t api_version_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", PS_API_VERSION);
}
static DEVICE_ATTR_RO(api_version);

static ssize_t thermal_profile_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	u8 tp;
	int err;

	err = ps_thermal_profile_get(&tp);
	if (err)
		return err;

	return sysfs_emit(buf, "%s\n", ps_thermal_profile_name(tp));
}
static DEVICE_ATTR_RO(thermal_profile);

static ssize_t thermal_profile_raw_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	u8 tp;
	int err;

	err = ps_thermal_profile_get(&tp);
	if (err)
		return err;

	return sysfs_emit(buf, "0x%02x\n", tp);
}
static DEVICE_ATTR_RO(thermal_profile_raw);

static struct attribute *ps_attrs[] = {
	&dev_attr_api_version.attr,
	&dev_attr_thermal_profile.attr,
	&dev_attr_thermal_profile_raw.attr,
	NULL,
};

static const struct attribute_group ps_attr_group = {
	.name = "perfect_sense",
	.attrs = ps_attrs,
};

int ps_sysfs_register(struct device *dev)
{
	return sysfs_create_group(&dev->kobj, &ps_attr_group);
}

void ps_sysfs_unregister(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &ps_attr_group);
}
