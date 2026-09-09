// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PerfectSense — WMI hotkey / AC notify → input + uevent (Phase 10).
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/wmi.h>

#include "perfectsense.h"

struct ps_event_value {
	u8 function;
	u8 key_num;
	u16 device_state;
	u16 reserved1;
	u8 kbd_dock_state;
	u8 reserved2;
} __packed;

enum {
	PS_EV_TURBO_KEY = 0x07,
	PS_EV_AC = 0x08,
};

static struct input_dev *ps_input;
static struct device *ps_event_parent;

static void ps_emit_uevent(const char *event, u8 key_num)
{
	char buf_event[64];
	char buf_key[32];
	char *envp[4];
	int i = 0;

	if (!ps_event_parent)
		return;

	snprintf(buf_event, sizeof(buf_event), "PERFECTSENSE_EVENT=%s", event);
	envp[i++] = buf_event;
	snprintf(buf_key, sizeof(buf_key), "PERFECTSENSE_KEY=%u", key_num);
	envp[i++] = buf_key;
	envp[i++] = NULL;
	kobject_uevent_env(&ps_event_parent->kobj, KOBJ_CHANGE, envp);
}

static void ps_report_key(unsigned int code)
{
	if (!ps_input)
		return;
	input_report_key(ps_input, code, 1);
	input_sync(ps_input);
	input_report_key(ps_input, code, 0);
	input_sync(ps_input);
}

static void ps_wmi_notify(union acpi_object *obj, void *context)
{
	struct ps_event_value ev;

	(void)context;

	if (!obj)
		return;
	if (obj->type != ACPI_TYPE_BUFFER) {
		pr_warn_ratelimited("notify: unexpected type %u\n", obj->type);
		return;
	}
	if (obj->buffer.length < sizeof(ev)) {
		pr_warn_ratelimited("notify: short buffer %u\n",
				    obj->buffer.length);
		return;
	}

	memcpy(&ev, obj->buffer.pointer, sizeof(ev));

	switch (ev.function) {
	case PS_EV_TURBO_KEY:
		/*
		 * Predator PHN16-72: 0x4 = turbo button, 0x5 = thermal mode.
		 * (Matches nekro / OEM Sense mapping for non–Nitro-V4.)
		 */
		if (ev.key_num == 0x4) {
			pr_info("turbo key\n");
			ps_report_key(KEY_PROG1);
			ps_emit_uevent("turbo", ev.key_num);
		} else if (ev.key_num == 0x5) {
			pr_info("mode key\n");
			ps_report_key(KEY_PROG2);
			ps_emit_uevent("mode", ev.key_num);
		} else {
			pr_info("gaming key %u\n", ev.key_num);
			ps_emit_uevent("key", ev.key_num);
		}
		break;
	case PS_EV_AC:
		if (ev.key_num == 0) {
			ps_emit_uevent("ac-offline", ev.key_num);
		} else if (ev.key_num == 1) {
			ps_emit_uevent("ac-online", ev.key_num);
		} else {
			ps_emit_uevent("ac", ev.key_num);
		}
		break;
	default:
		break;
	}
}

int ps_events_register(struct device *parent)
{
	acpi_status status;
	int err;

	if (!parent)
		return -EINVAL;

	if (!wmi_has_guid(PS_EVENT_GUID)) {
		pr_warn("event GUID missing; hotkeys disabled\n");
		return 0;
	}

	ps_input = input_allocate_device();
	if (!ps_input)
		return -ENOMEM;

	ps_input->name = "PerfectSense Hotkeys";
	ps_input->phys = "perfectsense/input0";
	ps_input->id.bustype = BUS_HOST;
	ps_input->dev.parent = parent;
	set_bit(EV_KEY, ps_input->evbit);
	set_bit(KEY_PROG1, ps_input->keybit);
	set_bit(KEY_PROG2, ps_input->keybit);

	err = input_register_device(ps_input);
	if (err) {
		input_free_device(ps_input);
		ps_input = NULL;
		return err;
	}

	ps_event_parent = parent;
	status = wmi_install_notify_handler(PS_EVENT_GUID, ps_wmi_notify, NULL);
	if (ACPI_FAILURE(status)) {
		pr_err("wmi notify install failed: %s\n",
		       acpi_format_exception(status));
		input_unregister_device(ps_input);
		ps_input = NULL;
		ps_event_parent = NULL;
		return -ENODEV;
	}

	pr_info("WMI hotkey notify registered\n");
	return 0;
}

void ps_events_inject(const char *event, u8 key_num)
{
	if (!event)
		return;
	if (!strcmp(event, "turbo"))
		ps_report_key(KEY_PROG1);
	else if (!strcmp(event, "mode"))
		ps_report_key(KEY_PROG2);
	ps_emit_uevent(event, key_num);
}

void ps_events_unregister(void)
{
	if (wmi_has_guid(PS_EVENT_GUID))
		wmi_remove_notify_handler(PS_EVENT_GUID);
	ps_event_parent = NULL;
	if (ps_input) {
		input_unregister_device(ps_input);
		ps_input = NULL;
	}
}
