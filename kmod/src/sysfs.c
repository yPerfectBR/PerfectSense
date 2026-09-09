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

static ssize_t battery_limiter_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	u8 on;
	int err = ps_battery_limiter_get(&on);

	if (err)
		return err;
	return sysfs_emit(buf, "%u\n", on);
}

static ssize_t battery_limiter_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	unsigned int val;
	int err;

	if (kstrtouint(buf, 0, &val) || val > 1)
		return -EINVAL;
	err = ps_battery_limiter_set((u8)val);
	return err ? err : count;
}
static DEVICE_ATTR_RW(battery_limiter);

static ssize_t battery_calibration_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	u8 on;
	int err = ps_battery_calibration_get(&on);

	if (err)
		return err;
	return sysfs_emit(buf, "%u\n", on);
}

static ssize_t battery_calibration_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	unsigned int val;
	int err;

	if (kstrtouint(buf, 0, &val) || val > 1)
		return -EINVAL;
	err = ps_battery_calibration_set((u8)val);
	return err ? err : count;
}
static DEVICE_ATTR_RW(battery_calibration);

static ssize_t usb_charging_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int minutes;
	int err = ps_usb_charging_get(&minutes);

	if (err == -ENODATA)
		return sysfs_emit(buf, "-1\n");
	if (err)
		return err;
	return sysfs_emit(buf, "%d\n", minutes);
}

static ssize_t usb_charging_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int minutes;
	int err;

	if (kstrtoint(buf, 0, &minutes))
		return -EINVAL;
	err = ps_usb_charging_set(minutes);
	return err ? err : count;
}
static DEVICE_ATTR_RW(usb_charging);

static ssize_t lcd_override_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int on;
	int err = ps_lcd_override_get(&on);

	if (err == -ENODATA)
		return sysfs_emit(buf, "-1\n");
	if (err)
		return err;
	return sysfs_emit(buf, "%d\n", on);
}

static ssize_t lcd_override_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned int val;
	int err;

	if (kstrtouint(buf, 0, &val) || val > 1)
		return -EINVAL;
	err = ps_lcd_override_set((u8)val);
	return err ? err : count;
}
static DEVICE_ATTR_RW(lcd_override);

static ssize_t backlight_timeout_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	int on;
	int err = ps_backlight_timeout_get(&on);

	if (err == -ENODATA)
		return sysfs_emit(buf, "-1\n");
	if (err)
		return err;
	return sysfs_emit(buf, "%d\n", on);
}

static ssize_t backlight_timeout_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	unsigned int val;
	int err;

	if (kstrtouint(buf, 0, &val) || val > 1)
		return -EINVAL;
	err = ps_backlight_timeout_set((u8)val);
	return err ? err : count;
}
static DEVICE_ATTR_RW(backlight_timeout);

static ssize_t boot_animation_sound_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	u8 on;
	int err = ps_boot_animation_sound_get(&on);

	if (err)
		return err;
	return sysfs_emit(buf, "%u\n", on);
}

static ssize_t boot_animation_sound_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	unsigned int val;
	int err;

	if (kstrtouint(buf, 0, &val) || val > 1)
		return -EINVAL;
	err = ps_boot_animation_sound_set((u8)val);
	return err ? err : count;
}
static DEVICE_ATTR_RW(boot_animation_sound);

static struct attribute *ps_attrs[] = {
	&dev_attr_api_version.attr,
	&dev_attr_thermal_profile.attr,
	&dev_attr_thermal_profile_raw.attr,
	&dev_attr_ac_online.attr,
	&dev_attr_fan_duty.attr,
	&dev_attr_fan_speed.attr,
	&dev_attr_battery_limiter.attr,
	&dev_attr_battery_calibration.attr,
	&dev_attr_usb_charging.attr,
	&dev_attr_lcd_override.attr,
	&dev_attr_backlight_timeout.attr,
	&dev_attr_boot_animation_sound.attr,
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
