// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — Phase 6 four-zone RGB keyboard + lid logo.
 */

#include <linux/acpi.h>
#include <asm/byteorder.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/wmi.h>

#include "perfectsense.h"

struct ps_kb_blob {
	u8 gm_return;
	u8 gm_output[15];
} __packed;

struct ps_zone_set {
	u8 zone;
	u8 red;
	u8 green;
	u8 blue;
} __packed;

static int ps_wmi_buf(u32 method_id, void *in, size_t in_len,
		      void *out_buf, size_t out_expect)
{
	struct acpi_buffer input = { in_len, in };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;

	status = wmi_evaluate_method(PS_WMID_GUID4, 0, method_id, &input,
				     out_buf ? &output : NULL);
	if (ACPI_FAILURE(status))
		return -EIO;

	if (!out_buf)
		return 0;

	obj = output.pointer;
	if (!obj || obj->type != ACPI_TYPE_BUFFER ||
	    obj->buffer.length < out_expect) {
		kfree(obj);
		return -ENOMSG;
	}

	memcpy(out_buf, obj->buffer.pointer, out_expect);
	kfree(obj);
	return 0;
}

static int ps_kb_zone_enable(void)
{
	int err;

	err = ps_wmi_method_u64(PS_WMID_GET_SYS_INFO, 0, NULL);
	if (err)
		return err;

	/* Enable RGB zones (same payload as nekro / acer gaming) */
	return ps_wmi_method_u64(PS_WMID_SET_GAMING_LED, 8ULL | (15ULL << 40),
				 NULL);
}

static int ps_kb_power_gate(u8 enable, u8 brightness)
{
	u8 bhkb[16] = {
		enable, 0, brightness, 0, 0, 0, 0, 0, 0, 1 /* KB */, 0, 0, 0, 0, 0, 0
	};

	return ps_wmi_buf(PS_WMID_SET_KB_BACKLIGHT, bhkb, sizeof(bhkb), NULL, 0);
}

static int ps_set_kb_status(u8 mode, u8 speed, u8 brightness, u8 direction,
			    u8 red, u8 green, u8 blue)
{
	u8 gm_input[16] = {
		mode, speed, brightness, 0, direction, red, green, blue, 3, 1, 0, 0, 0, 0, 0, 0
	};
	u8 out[16];
	int err;

	err = ps_wmi_buf(PS_WMID_SET_KB_BACKLIGHT, gm_input, sizeof(gm_input),
			 out, sizeof(out));
	/* Some firmwares return empty/short; treat as OK if evaluate succeeded */
	if (err == -ENOMSG)
		return 0;
	if (err)
		return err;
	if (out[0] != 0)
		return -EIO;
	return 0;
}

static int ps_get_kb_status(struct ps_kb_blob *out)
{
	u64 in = 1;

	return ps_wmi_buf(PS_WMID_GET_KB_BACKLIGHT, &in, sizeof(in), out,
			  sizeof(*out));
}

int ps_kb_effect_get(struct ps_kb_effect *out)
{
	struct ps_kb_blob blob;
	int err;

	if (!out)
		return -EINVAL;

	err = ps_get_kb_status(&blob);
	if (err)
		return err;

	out->mode = blob.gm_output[0];
	out->speed = blob.gm_output[1];
	out->brightness = blob.gm_output[2];
	out->direction = blob.gm_output[4];
	out->red = blob.gm_output[5];
	out->green = blob.gm_output[6];
	out->blue = blob.gm_output[7];
	return 0;
}

int ps_kb_effect_set(const struct ps_kb_effect *in)
{
	struct ps_kb_effect e;
	int err;

	if (!in)
		return -EINVAL;

	e = *in;
	if (e.mode > 7 || e.speed > 9 || e.brightness > 100 || e.direction > 2)
		return -EINVAL;

	switch (e.mode) {
	case 0:
	case 1:
		e.speed = 0;
		e.direction = 0;
		break;
	case 2:
		e.red = e.green = e.blue = 0;
		e.direction = 0;
		break;
	case 3:
		e.red = e.green = e.blue = 0;
		break;
	case 5:
	case 6:
	case 7:
		e.direction = 0;
		break;
	case 4:
	default:
		break;
	}

	if (e.mode == 0 || e.mode == 1 || e.mode == 4) {
		err = ps_kb_zone_enable();
		if (err)
			return err;
	}

	return ps_set_kb_status(e.mode, e.speed, e.brightness, e.direction,
				e.red, e.green, e.blue);
}

int ps_kb_zones_get(struct ps_kb_zones *out)
{
	static const u8 zone_ids[4] = { 0x1, 0x2, 0x4, 0x8 };
	struct ps_kb_effect effect;
	u64 raw;
	int i, err;

	if (!out)
		return -EINVAL;

	for (i = 0; i < 4; i++) {
		err = ps_wmi_method_u64(PS_WMID_GET_RGB_KB, zone_ids[i], &raw);
		if (err)
			return err;
		/* Firmware returns packed colour; normalize like nekro */
		out->zone[i] = (u32)(cpu_to_be64(raw) >> 32) & 0xffffff;
	}

	err = ps_kb_effect_get(&effect);
	if (err)
		return err;
	out->brightness = effect.brightness;
	return 0;
}

int ps_kb_zones_set(const struct ps_kb_zones *in)
{
	static const u8 zone_ids[4] = { 0x1, 0x2, 0x4, 0x8 };
	int i, err;

	if (!in || in->brightness > 100)
		return -EINVAL;

	err = ps_kb_zone_enable();
	if (err)
		return err;

	err = ps_kb_power_gate(1, in->brightness);
	if (err)
		return err;

	/* Clear effect layer before static colours */
	err = ps_set_kb_status(0, 0, 0, 0, 0, 0, 0);
	if (err)
		return err;

	for (i = 0; i < 4; i++) {
		struct ps_zone_set p = {
			.zone = zone_ids[i],
			.red = (in->zone[i] >> 16) & 0xff,
			.green = (in->zone[i] >> 8) & 0xff,
			.blue = in->zone[i] & 0xff,
		};

		err = ps_wmi_buf(PS_WMID_SET_RGB_KB, &p, sizeof(p), NULL, 0);
		if (err)
			return err;
	}

	err = ps_kb_power_gate(1, in->brightness);
	if (err)
		return err;

	return ps_set_kb_status(0, 0, in->brightness, 0, 0, 0, 0);
}

int ps_kb_off(void)
{
	struct ps_kb_zones z = { .zone = { 0, 0, 0, 0 }, .brightness = 0 };

	return ps_kb_zones_set(&z);
}

int ps_logo_get(struct ps_logo *out)
{
	u8 req = 1;
	u8 col[6];
	int err;

	if (!out)
		return -EINVAL;

	err = ps_wmi_buf(PS_WMID_GET_LOGO, &req, sizeof(req), col, sizeof(col));
	if (!err) {
		/* bytes: status,r,g,b,brightness,enable (nekro layout) */
		out->red = col[1];
		out->green = col[2];
		out->blue = col[3];
		out->brightness = col[4];
		out->enable = col[5] ? 1 : 0;
		return 0;
	}

	/* Fallback: unified KB backlight getter with LB selector */
	{
		u64 sel = 2;
		struct ps_kb_blob blob;

		err = ps_wmi_buf(PS_WMID_GET_KB_BACKLIGHT, &sel, sizeof(sel),
				 &blob, sizeof(blob));
		if (err)
			return err;
		out->enable = blob.gm_output[0] ? 1 : 0;
		out->brightness = blob.gm_output[2];
		out->red = blob.gm_output[5];
		out->green = blob.gm_output[6];
		out->blue = blob.gm_output[7];
		return 0;
	}
}

int ps_logo_set(const struct ps_logo *in)
{
	u8 enable, brightness;
	u8 bhgk[6];
	u8 bhlk[16];
	int err;

	if (!in || in->brightness > 100 || in->enable > 1)
		return -EINVAL;

	enable = in->enable;
	brightness = in->brightness;
	if (!enable)
		brightness = 0;

	bhgk[0] = 1; /* LB select */
	bhgk[1] = in->red;
	bhgk[2] = in->green;
	bhgk[3] = in->blue;
	bhgk[4] = brightness;
	bhgk[5] = enable;

	err = ps_wmi_buf(PS_WMID_SET_LOGO, bhgk, sizeof(bhgk), NULL, 0);
	if (err)
		return err;

	memset(bhlk, 0, sizeof(bhlk));
	bhlk[0] = enable;
	bhlk[9] = 2; /* select LB */
	return ps_wmi_buf(PS_WMID_SET_KB_BACKLIGHT, bhlk, sizeof(bhlk), NULL, 0);
}

int ps_lighting_reset(u8 val)
{
	u8 cmd[16] = { 0 };

	cmd[0] = val;
	return ps_wmi_buf(PS_WMID_SET_GAMING_LED, cmd, sizeof(cmd), NULL, 0);
}
