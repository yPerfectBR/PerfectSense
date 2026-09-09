// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — WMI helpers (gaming misc + AC status).
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/unaligned.h>
#include <linux/wmi.h>

#include "perfectsense.h"

static int ps_wmi_eval_u64(u32 method_id, u64 in, u64 *out)
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

static int ps_wmi_eval_u32_in_u64_out(u32 method_id, u32 in, u64 *out)
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

	ret = ps_wmi_eval_u32_in_u64_out(PS_WMID_GET_MISC, (u32)input, &result);
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

	ret = ps_wmi_eval_u64(PS_WMID_SET_MISC, input, &result);
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

	ret = ps_wmi_eval_u64(PS_WMID_GET_SYS_INFO, PS_CMD_BAT_STATUS, &on_ac);
	if (ret < 0)
		return ret;

	*online = on_ac != 0;
	return 0;
}
