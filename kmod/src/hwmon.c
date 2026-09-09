// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — Predator V4 WMI sensors as hwmon (temps + fan RPM).
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/hwmon.h>
#include <linux/module.h>

#include "perfectsense.h"

#define PS_SENSOR_INDEX_MASK		GENMASK_ULL(15, 8)
#define PS_SENSOR_READING_MASK		GENMASK_ULL(23, 8)
#define PS_SUPPORTED_SENSORS_MASK	GENMASK_ULL(39, 24)

#define PS_CMD_SUPPORTED_SENSORS	0x0000
#define PS_CMD_SENSOR_READING		0x0001

enum ps_sensor_id {
	PS_SENSOR_CPU_TEMP	= 0x01,
	PS_SENSOR_CPU_FAN	= 0x02,
	PS_SENSOR_EXT_TEMP2	= 0x03,
	PS_SENSOR_GPU_FAN	= 0x06,
	PS_SENSOR_GPU_TEMP	= 0x0A,
};

static const enum ps_sensor_id ps_temp_sensors[] = {
	PS_SENSOR_CPU_TEMP,
	PS_SENSOR_GPU_TEMP,
	PS_SENSOR_EXT_TEMP2,
};

static const enum ps_sensor_id ps_fan_sensors[] = {
	PS_SENSOR_CPU_FAN,
	PS_SENSOR_GPU_FAN,
};

static u64 ps_supported_sensors;
static struct device *ps_hwmon_dev;

static umode_t ps_hwmon_is_visible(const void *data,
				   enum hwmon_sensor_types type, u32 attr,
				   int channel)
{
	const u64 *supported = data;
	enum ps_sensor_id id;

	(void)attr;

	switch (type) {
	case hwmon_temp:
		if (channel < 0 ||
		    channel >= (int)ARRAY_SIZE(ps_temp_sensors))
			return 0;
		id = ps_temp_sensors[channel];
		break;
	case hwmon_fan:
		if (channel < 0 ||
		    channel >= (int)ARRAY_SIZE(ps_fan_sensors))
			return 0;
		id = ps_fan_sensors[channel];
		break;
	default:
		return 0;
	}

	if (*supported & BIT(id - 1))
		return 0444;
	return 0;
}

static int ps_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long *val)
{
	u64 command = PS_CMD_SENSOR_READING;
	u64 result;
	enum ps_sensor_id id;
	int err;

	(void)dev;
	(void)attr;

	switch (type) {
	case hwmon_temp:
		if (channel < 0 ||
		    channel >= (int)ARRAY_SIZE(ps_temp_sensors))
			return -EOPNOTSUPP;
		id = ps_temp_sensors[channel];
		command |= FIELD_PREP(PS_SENSOR_INDEX_MASK, id);
		err = ps_wmi_method_u64(PS_WMID_GET_SYS_INFO, command, &result);
		if (err)
			return err;
		*val = (long)FIELD_GET(PS_SENSOR_READING_MASK, result) * 1000L;
		return 0;
	case hwmon_fan:
		if (channel < 0 ||
		    channel >= (int)ARRAY_SIZE(ps_fan_sensors))
			return -EOPNOTSUPP;
		id = ps_fan_sensors[channel];
		command |= FIELD_PREP(PS_SENSOR_INDEX_MASK, id);
		err = ps_wmi_method_u64(PS_WMID_GET_SYS_INFO, command, &result);
		if (err)
			return err;
		*val = (long)FIELD_GET(PS_SENSOR_READING_MASK, result);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_channel_info *const ps_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT, HWMON_T_INPUT, HWMON_T_INPUT),
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT, HWMON_F_INPUT),
	NULL,
};

static const struct hwmon_ops ps_hwmon_ops = {
	.is_visible = ps_hwmon_is_visible,
	.read = ps_hwmon_read,
};

static const struct hwmon_chip_info ps_hwmon_chip_info = {
	.ops = &ps_hwmon_ops,
	.info = ps_hwmon_info,
};

int ps_hwmon_register(struct device *parent)
{
	u64 result;
	int err;

	if (!parent)
		return -EINVAL;

	err = ps_wmi_method_u64(PS_WMID_GET_SYS_INFO, PS_CMD_SUPPORTED_SENSORS,
				&result);
	if (err) {
		pr_warn("sensor probe failed: %d\n", err);
		return 0;
	}

	ps_supported_sensors = FIELD_GET(PS_SUPPORTED_SENSORS_MASK, result);
	if (!ps_supported_sensors) {
		pr_info("no Predator V4 sensors reported\n");
		return 0;
	}

	ps_hwmon_dev = hwmon_device_register_with_info(parent, "perfectsense",
						       &ps_supported_sensors,
						       &ps_hwmon_chip_info,
						       NULL);
	if (IS_ERR(ps_hwmon_dev)) {
		err = PTR_ERR(ps_hwmon_dev);
		ps_hwmon_dev = NULL;
		pr_err("hwmon register failed: %d\n", err);
		return err;
	}

	pr_info("hwmon registered (sensors=0x%llx)\n",
		(unsigned long long)ps_supported_sensors);
	return 0;
}

void ps_hwmon_unregister(void)
{
	if (ps_hwmon_dev) {
		hwmon_device_unregister(ps_hwmon_dev);
		ps_hwmon_dev = NULL;
	}
	ps_supported_sensors = 0;
}
