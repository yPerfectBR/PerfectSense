// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — sysfs group under perfect_sense/
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/string.h>
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

static ssize_t thermal_profile_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	char name[32];
	u8 tp;
	int err;
	size_t len;

	len = min(count, sizeof(name) - 1);
	memcpy(name, buf, len);
	name[len] = '\0';
	strim(name);

	err = ps_thermal_profile_from_name(name, &tp);
	if (err)
		return err;

	err = ps_thermal_profile_set(tp);
	if (err)
		return err;

	return count;
}
static DEVICE_ATTR_RW(thermal_profile);

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

static ssize_t ac_online_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	bool online;
	int err;

	err = ps_wmi_ac_online(&online);
	if (err)
		return err;

	return sysfs_emit(buf, "%u\n", online ? 1 : 0);
}
static DEVICE_ATTR_RO(ac_online);

static ssize_t fan_duty_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	u8 cpu_duty, gpu_duty;
	int err;

	err = ps_fan_get_duty(&cpu_duty, &gpu_duty);
	if (err)
		return err;

	return sysfs_emit(buf, "%u,%u\n", cpu_duty, gpu_duty);
}
static DEVICE_ATTR_RO(fan_duty);

static ssize_t fan_speed_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	u8 cpu_pct, gpu_pct;

	ps_fan_get_target(&cpu_pct, &gpu_pct);
	return sysfs_emit(buf, "%u,%u\n", cpu_pct, gpu_pct);
}

static ssize_t fan_speed_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	unsigned int cpu_pct, gpu_pct;
	int err;

	if (sscanf(buf, "%u,%u", &cpu_pct, &gpu_pct) != 2)
		return -EINVAL;

	if (cpu_pct > 100 || gpu_pct > 100)
		return -EINVAL;

	err = ps_fan_set((u8)cpu_pct, (u8)gpu_pct);
	if (err)
		return err;

	return count;
}
static DEVICE_ATTR_RW(fan_speed);

static struct attribute *ps_attrs[] = {
	&dev_attr_api_version.attr,
	&dev_attr_thermal_profile.attr,
	&dev_attr_thermal_profile_raw.attr,
	&dev_attr_ac_online.attr,
	&dev_attr_fan_duty.attr,
	&dev_attr_fan_speed.attr,
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
