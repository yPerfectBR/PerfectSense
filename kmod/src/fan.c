// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — CPU/GPU fan duty (live get + set/auto).
 */

#include <linux/bitfield.h>
#include <linux/module.h>

#include "perfectsense.h"

/* Last userspace target; 0,0 means firmware auto. */
static u8 ps_fan_target_cpu;
static u8 ps_fan_target_gpu;

static u64 ps_fan_val_calc(u8 percentage, u8 fan_index)
{
	return (((percentage * 25600) / 100) & 0xFF00) + fan_index;
}

static int ps_fan_get_one(u8 fan_id, u8 *speed)
{
	u32 input = 0;
	u64 result;
	int ret;

	input |= FIELD_PREP(PS_FAN_ID_MASK, fan_id);
	ret = ps_wmi_method_u32_u64(PS_WMID_GET_FAN_SPEED, input, &result);
	if (ret < 0)
		return ret;

	if (FIELD_GET(PS_FAN_STATUS_MASK, result))
		return -ENODEV;

	*speed = FIELD_GET(PS_FAN_VALUE_MASK, result);
	return 0;
}

int ps_fan_get_duty(u8 *cpu_duty, u8 *gpu_duty)
{
	int ret;

	if (!cpu_duty || !gpu_duty)
		return -EINVAL;

	ret = ps_fan_get_one(PS_FAN_ID_CPU, cpu_duty);
	if (ret)
		return ret;

	return ps_fan_get_one(PS_FAN_ID_GPU, gpu_duty);
}

void ps_fan_get_target(u8 *cpu_pct, u8 *gpu_pct)
{
	if (cpu_pct)
		*cpu_pct = ps_fan_target_cpu;
	if (gpu_pct)
		*gpu_pct = ps_fan_target_gpu;
}

int ps_fan_set_auto(void)
{
	int ret;

	ret = ps_wmi_method_u64(PS_WMID_SET_FAN_BEHAVIOR, 0x410009, NULL);
	if (ret)
		return ret;

	ps_fan_target_cpu = 0;
	ps_fan_target_gpu = 0;
	return 0;
}

static int ps_fan_set_max(void)
{
	int ret;

	ret = ps_wmi_method_u64(PS_WMID_SET_FAN_BEHAVIOR, 0x820009, NULL);
	if (ret)
		return ret;

	ps_fan_target_cpu = 100;
	ps_fan_target_gpu = 100;
	return 0;
}

int ps_fan_set(u8 cpu_pct, u8 gpu_pct)
{
	int ret;

	if (cpu_pct > 100 || gpu_pct > 100)
		return -EINVAL;

	if (cpu_pct == 0 && gpu_pct == 0)
		return ps_fan_set_auto();

	if (cpu_pct == 100 && gpu_pct == 100)
		return ps_fan_set_max();

	if (cpu_pct == 0) {
		/* GPU custom, CPU auto on that axis */
		ret = ps_wmi_method_u64(PS_WMID_SET_FAN_BEHAVIOR, 0x10001, NULL);
		if (ret)
			return ret;
		ret = ps_wmi_method_u64(PS_WMID_SET_FAN_BEHAVIOR, 0xC00008, NULL);
		if (ret)
			return ret;
		ret = ps_wmi_method_u64(PS_WMID_SET_FAN_SPEED,
					ps_fan_val_calc(gpu_pct, PS_FAN_ID_GPU), NULL);
		if (ret)
			return ret;
	} else if (gpu_pct == 0) {
		ret = ps_wmi_method_u64(PS_WMID_SET_FAN_BEHAVIOR, 0x400008, NULL);
		if (ret)
			return ret;
		ret = ps_wmi_method_u64(PS_WMID_SET_FAN_BEHAVIOR, 0x30001, NULL);
		if (ret)
			return ret;
		ret = ps_wmi_method_u64(PS_WMID_SET_FAN_SPEED,
					ps_fan_val_calc(cpu_pct, PS_FAN_ID_CPU), NULL);
		if (ret)
			return ret;
	} else {
		ret = ps_wmi_method_u64(PS_WMID_SET_FAN_BEHAVIOR, 0xC30009, NULL);
		if (ret)
			return ret;
		ret = ps_wmi_method_u64(PS_WMID_SET_FAN_SPEED,
					ps_fan_val_calc(cpu_pct, PS_FAN_ID_CPU), NULL);
		if (ret)
			return ret;
		ret = ps_wmi_method_u64(PS_WMID_SET_FAN_SPEED,
					ps_fan_val_calc(gpu_pct, PS_FAN_ID_GPU), NULL);
		if (ret)
			return ret;
	}

	ps_fan_target_cpu = cpu_pct;
	ps_fan_target_gpu = gpu_pct;
	return 0;
}
