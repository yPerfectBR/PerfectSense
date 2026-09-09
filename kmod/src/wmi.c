// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — WMI helpers (gaming misc + AC status + raw methods).
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/unaligned.h>
#include <linux/wmi.h>

#include "perfectsense.h"

int ps_wmi_method_u64(u32 method_id, u64 in, u64 *out)
{
	struct acpi_buffer input = { sizeof(in), &in };
	struct acpi_buffer result = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	u64 tmp = 0;

	status = wmi_evaluate_method(PS_WMID_GUID4, 0, method_id, &input, &result);
	if (ACPI_FAILURE(status))
		return -EIO;

	obj = result.pointer;
	if (obj) {
		if (obj->type == ACPI_TYPE_BUFFER) {
			if (obj->buffer.length == sizeof(u32))
				tmp = *(u32 *)obj->buffer.pointer;
			else if (obj->buffer.length >= sizeof(u64))
				tmp = get_unaligned_le64(obj->buffer.pointer);
		} else if (obj->type == ACPI_TYPE_INTEGER) {
			tmp = obj->integer.value;
		}
	}

	if (out)
		*out = tmp;

	kfree(result.pointer);
	return 0;
}

int ps_wmi_method_u32_u64(u32 method_id, u32 in, u64 *out)
{
	struct acpi_buffer result = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer input = {
		.length = sizeof(in),
		.pointer = &in,
	};
	union acpi_object *obj;
	acpi_status status;
	int ret = 0;

	status = wmi_evaluate_method(PS_WMID_GUID4, 0, method_id, &input, &result);
	if (ACPI_FAILURE(status))
		return -EIO;

	obj = result.pointer;
	if (obj && out) {
		switch (obj->type) {
		case ACPI_TYPE_INTEGER:
			*out = obj->integer.value;
			break;
		case ACPI_TYPE_BUFFER:
			if (obj->buffer.length < sizeof(*out))
				ret = -ENOMSG;
			else
				*out = get_unaligned_le64(obj->buffer.pointer);
			break;
		default:
			ret = -ENOMSG;
			break;
		}
	}

	kfree(obj);
	return ret;
}

int ps_wmi_get_misc(u8 index, u8 *value)
{
	u64 input = 0;
	u64 result;
	int ret;

	if (!value)
		return -EINVAL;

	input |= FIELD_PREP(PS_MISC_INDEX_MASK, index);

	ret = ps_wmi_method_u32_u64(PS_WMID_GET_MISC, (u32)input, &result);
	if (ret < 0)
		return ret;

	if (FIELD_GET(PS_MISC_STATUS_MASK, result))
		return -EIO;

	*value = FIELD_GET(PS_MISC_VALUE_MASK, result);
	return 0;
}

int ps_wmi_set_misc(u8 index, u8 value)
{
	u64 input = 0;
	u64 result;
	int ret;

	input |= FIELD_PREP(PS_MISC_INDEX_MASK, index);
	input |= FIELD_PREP(PS_MISC_VALUE_MASK, value);

	ret = ps_wmi_method_u64(PS_WMID_SET_MISC, input, &result);
	if (ret < 0)
		return ret;

	if (FIELD_GET(PS_MISC_STATUS_MASK, result))
		return -EIO;

	return 0;
}

int ps_wmi_ac_online(bool *online)
{
	u64 on_ac;
	int ret;

	if (!online)
		return -EINVAL;

	ret = ps_wmi_method_u64(PS_WMID_GET_SYS_INFO, PS_CMD_BAT_STATUS, &on_ac);
	if (ret < 0)
		return ret;

	*online = on_ac != 0;
	return 0;
}

int ps_wmi_apge_u64(u32 method_id, u64 in, u64 *out)
{
	struct acpi_buffer input = { sizeof(in), &in };
	struct acpi_buffer result = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;
	u64 tmp = 0;

	if (!wmi_has_guid(PS_WMID_GUID3))
		return -ENODEV;

	status = wmi_evaluate_method(PS_WMID_GUID3, 0, method_id, &input, &result);
	if (ACPI_FAILURE(status))
		return -EIO;

	obj = result.pointer;
	if (obj) {
		if (obj->type == ACPI_TYPE_BUFFER) {
			if (obj->buffer.length == sizeof(u32))
				tmp = *(u32 *)obj->buffer.pointer;
			else if (obj->buffer.length >= sizeof(u64))
				tmp = get_unaligned_le64(obj->buffer.pointer);
		} else if (obj->type == ACPI_TYPE_INTEGER) {
			tmp = obj->integer.value;
		}
	}

	if (out)
		*out = tmp;

	kfree(result.pointer);
	return 0;
}

struct ps_bat_health_get_in {
	u8 battery_no;
	u8 function_query;
	u8 reserved[2];
} __packed;

struct ps_bat_health_get_out {
	u8 function_list;
	u8 ret[2];
	u8 function_status[5];
} __packed;

struct ps_bat_health_set_in {
	u8 battery_no;
	u8 function_mask;
	u8 function_status;
	u8 reserved[5];
} __packed;

struct ps_bat_health_set_out {
	u8 ret;
	u8 reserved;
} __packed;

int ps_wmi_battery_health_get(u8 mode, u8 *enabled)
{
	struct ps_bat_health_get_in params = {
		.battery_no = 0x1,
		.function_query = 0x1,
		.reserved = { 0, 0 },
	};
	struct acpi_buffer input = { sizeof(params), &params };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	struct ps_bat_health_get_out *ret;
	acpi_status status;

	if (!enabled)
		return -EINVAL;
	if (mode != PS_BAT_HEALTH_MODE && mode != PS_BAT_CALIBRATION_MODE)
		return -EINVAL;
	if (!wmi_has_guid(PS_WMID_GUID5))
		return -ENODEV;

	status = wmi_evaluate_method(PS_WMID_GUID5, 0, PS_WMID_GET_BATTERY_HEALTH,
				     &input, &output);
	if (ACPI_FAILURE(status))
		return -EIO;

	obj = output.pointer;
	if (!obj || obj->type != ACPI_TYPE_BUFFER || obj->buffer.length < 8) {
		kfree(obj);
		return -ENOMSG;
	}

	ret = (struct ps_bat_health_get_out *)obj->buffer.pointer;
	if (mode == PS_BAT_HEALTH_MODE)
		*enabled = ret->function_status[0] ? 1 : 0;
	else
		*enabled = ret->function_status[1] ? 1 : 0;

	kfree(obj);
	return 0;
}

int ps_wmi_battery_health_set(u8 mode, u8 enabled)
{
	struct ps_bat_health_set_in params = {
		.battery_no = 0x1,
		.function_mask = mode,
		.function_status = enabled ? 1 : 0,
		.reserved = { 0, 0, 0, 0, 0 },
	};
	struct acpi_buffer input = { sizeof(params), &params };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	struct ps_bat_health_set_out *ret;
	acpi_status status;

	if (mode != PS_BAT_HEALTH_MODE && mode != PS_BAT_CALIBRATION_MODE)
		return -EINVAL;
	if (enabled > 1)
		return -EINVAL;
	if (!wmi_has_guid(PS_WMID_GUID5))
		return -ENODEV;

	status = wmi_evaluate_method(PS_WMID_GUID5, 0, PS_WMID_SET_BATTERY_HEALTH,
				     &input, &output);
	if (ACPI_FAILURE(status))
		return -EIO;

	obj = output.pointer;
	if (!obj || obj->type != ACPI_TYPE_BUFFER || obj->buffer.length < 2) {
		kfree(obj);
		return -ENOMSG;
	}

	ret = (struct ps_bat_health_set_out *)obj->buffer.pointer;
	if (ret->ret != 0 && ret->reserved != 0) {
		kfree(obj);
		return -EIO;
	}

	kfree(obj);
	return 0;
}
