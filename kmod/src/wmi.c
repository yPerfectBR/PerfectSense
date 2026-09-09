// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — WMI helpers (gaming misc settings).
 */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/unaligned.h>
#include <linux/wmi.h>

#include "perfectsense.h"

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

int ps_thermal_profile_get(u8 *tp)
{
	return ps_wmi_get_misc(PS_MISC_PLATFORM_PROFILE, tp);
}

const char *ps_thermal_profile_name(u8 tp)
{
	switch (tp) {
	case PS_TP_ECO:
		return "low-power";
	case PS_TP_QUIET:
		return "quiet";
	case PS_TP_BALANCED:
		return "balanced";
	case PS_TP_PERFORMANCE:
		return "balanced-performance";
	case PS_TP_TURBO:
		return "performance";
	default:
		return "unknown";
	}
}
