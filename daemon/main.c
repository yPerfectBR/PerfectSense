/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * perfectsensed — system D-Bus daemon for org.perfectsense.Sense1
 *
 * Talks to PerfectSense sysfs via libperfectsense. No FanAggressor timer.
 * CLI continues to work without this process.
 */

#include "perfectsense.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>

#define PS_DBUS_NAME  "org.perfectsense.Sense1"
#define PS_DBUS_PATH  "/org/perfectsense/Sense1"
#define PS_DBUS_IFACE "org.perfectsense.Sense1"

static volatile sig_atomic_t g_stop;
static sd_bus *g_bus;
static char g_last_profile[64];

static void on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static int emit_profile_changed(const char *profile)
{
	return sd_bus_emit_signal(g_bus, PS_DBUS_PATH, PS_DBUS_IFACE,
				  "ProfileChanged", "s", profile);
}

static int prop_api_version(sd_bus *bus, const char *path, const char *iface,
			    const char *prop, sd_bus_message *reply,
			    void *userdata, sd_bus_error *err)
{
	struct ps_status st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;

	r = ps_status_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "u", st.api_version);
}

static int prop_available(sd_bus *bus, const char *path, const char *iface,
			  const char *prop, sd_bus_message *reply,
			  void *userdata, sd_bus_error *err)
{
	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	(void)err;
	return sd_bus_message_append(reply, "b", ps_available() == 0);
}

static int prop_thermal_profile_get(sd_bus *bus, const char *path,
				    const char *iface, const char *prop,
				    sd_bus_message *reply, void *userdata,
				    sd_bus_error *err)
{
	char buf[64];
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;

	r = ps_mode_get(buf, sizeof(buf));
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "s", buf);
}

static int prop_thermal_profile_set(sd_bus *bus, const char *path,
				    const char *iface, const char *prop,
				    sd_bus_message *value, void *userdata,
				    sd_bus_error *err)
{
	const char *name = NULL;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;

	r = sd_bus_message_read(value, "s", &name);
	if (r < 0)
		return r;
	r = ps_mode_set(name);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);

	snprintf(g_last_profile, sizeof(g_last_profile), "%s", name);
	(void)emit_profile_changed(name);
	return 1;
}

static int prop_platform_profile(sd_bus *bus, const char *path,
				 const char *iface, const char *prop,
				 sd_bus_message *reply, void *userdata,
				 sd_bus_error *err)
{
	struct ps_status st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;

	r = ps_status_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "s", st.platform_profile);
}

static int prop_ac_online(sd_bus *bus, const char *path, const char *iface,
			  const char *prop, sd_bus_message *reply,
			  void *userdata, sd_bus_error *err)
{
	struct ps_status st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;

	r = ps_status_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "b", st.ac_online != 0);
}

static int prop_fan_duty_cpu(sd_bus *bus, const char *path, const char *iface,
			     const char *prop, sd_bus_message *reply,
			     void *userdata, sd_bus_error *err)
{
	struct ps_status st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = ps_status_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "u", st.fan_duty_cpu);
}

static int prop_fan_duty_gpu(sd_bus *bus, const char *path, const char *iface,
			     const char *prop, sd_bus_message *reply,
			     void *userdata, sd_bus_error *err)
{
	struct ps_status st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = ps_status_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "u", st.fan_duty_gpu);
}

static int prop_fan_target_cpu(sd_bus *bus, const char *path, const char *iface,
			       const char *prop, sd_bus_message *reply,
			       void *userdata, sd_bus_error *err)
{
	struct ps_status st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = ps_status_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "u", st.fan_target_cpu);
}

static int prop_fan_target_gpu(sd_bus *bus, const char *path, const char *iface,
			       const char *prop, sd_bus_message *reply,
			       void *userdata, sd_bus_error *err)
{
	struct ps_status st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = ps_status_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "u", st.fan_target_gpu);
}

static int prop_fan_auto(sd_bus *bus, const char *path, const char *iface,
			 const char *prop, sd_bus_message *reply,
			 void *userdata, sd_bus_error *err)
{
	struct ps_status st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = ps_status_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "b", st.fan_auto != 0);
}

static int prop_battery_limiter(sd_bus *bus, const char *path,
				const char *iface, const char *prop,
				sd_bus_message *reply, void *userdata,
				sd_bus_error *err)
{
	struct ps_status st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = ps_status_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "i", st.battery_limiter);
}

static int prop_usb_charging(sd_bus *bus, const char *path, const char *iface,
			     const char *prop, sd_bus_message *reply,
			     void *userdata, sd_bus_error *err)
{
	struct ps_status st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = ps_status_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "i", st.usb_charging);
}

static int prop_rgb(sd_bus *bus, const char *path, const char *iface,
		    const char *prop, sd_bus_message *reply, void *userdata,
		    sd_bus_error *err)
{
	struct ps_status st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = ps_status_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "s", st.rgb);
}

static int prop_logo(sd_bus *bus, const char *path, const char *iface,
		     const char *prop, sd_bus_message *reply, void *userdata,
		     sd_bus_error *err)
{
	struct ps_status st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = ps_status_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "s", st.logo);
}

static int method_set_fan_target(sd_bus_message *m, void *userdata,
				 sd_bus_error *err)
{
	uint32_t cpu, gpu;
	int r;

	(void)userdata;
	r = sd_bus_message_read(m, "uu", &cpu, &gpu);
	if (r < 0)
		return r;
	r = ps_fan_set(cpu, gpu);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	sd_bus_emit_properties_changed(
		g_bus, PS_DBUS_PATH, PS_DBUS_IFACE, "FanTargetCpu",
		"FanTargetGpu", "FanAuto", "FanDutyCpu", "FanDutyGpu", NULL);
	return sd_bus_reply_method_return(m, NULL);
}

static int method_set_fan_auto(sd_bus_message *m, void *userdata,
			       sd_bus_error *err)
{
	int r;

	(void)userdata;
	r = ps_fan_set_auto();
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	sd_bus_emit_properties_changed(
		g_bus, PS_DBUS_PATH, PS_DBUS_IFACE, "FanTargetCpu",
		"FanTargetGpu", "FanAuto", NULL);
	return sd_bus_reply_method_return(m, NULL);
}

static int method_set_rgb(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
	const char *csv = NULL;
	int r;

	(void)userdata;
	r = sd_bus_message_read(m, "s", &csv);
	if (r < 0)
		return r;
	r = ps_rgb_set(csv);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	sd_bus_emit_properties_changed(g_bus, PS_DBUS_PATH, PS_DBUS_IFACE,
				       "Rgb", NULL);
	return sd_bus_reply_method_return(m, NULL);
}

static int method_set_logo(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
	const char *csv = NULL;
	int r;

	(void)userdata;
	r = sd_bus_message_read(m, "s", &csv);
	if (r < 0)
		return r;
	r = ps_logo_set(csv);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	sd_bus_emit_properties_changed(g_bus, PS_DBUS_PATH, PS_DBUS_IFACE,
				       "Logo", NULL);
	return sd_bus_reply_method_return(m, NULL);
}

static int method_refresh(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
	char profile[64];
	int r;

	(void)userdata;
	(void)err;

	r = ps_mode_get(profile, sizeof(profile));
	if (r == 0 && strcmp(profile, g_last_profile) != 0) {
		snprintf(g_last_profile, sizeof(g_last_profile), "%s", profile);
		emit_profile_changed(profile);
	}

	sd_bus_emit_properties_changed(
		g_bus, PS_DBUS_PATH, PS_DBUS_IFACE, "ThermalProfile",
		"PlatformProfile", "AcOnline", "FanDutyCpu", "FanDutyGpu",
		"FanTargetCpu", "FanTargetGpu", "FanAuto", "BatteryLimiter",
		"UsbCharging", "Rgb", "Logo", "ApiVersion", "Available", NULL);
	return sd_bus_reply_method_return(m, NULL);
}

static const sd_bus_vtable sense1_vtable[] = {
	SD_BUS_VTABLE_START(0),

	SD_BUS_PROPERTY("ApiVersion", "u", prop_api_version, 0,
			SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Available", "b", prop_available, 0, 0),
	SD_BUS_WRITABLE_PROPERTY("ThermalProfile", "s", prop_thermal_profile_get,
				 prop_thermal_profile_set, 0,
				 SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_PROPERTY("PlatformProfile", "s", prop_platform_profile, 0, 0),
	SD_BUS_PROPERTY("AcOnline", "b", prop_ac_online, 0, 0),
	SD_BUS_PROPERTY("FanDutyCpu", "u", prop_fan_duty_cpu, 0, 0),
	SD_BUS_PROPERTY("FanDutyGpu", "u", prop_fan_duty_gpu, 0, 0),
	SD_BUS_PROPERTY("FanTargetCpu", "u", prop_fan_target_cpu, 0, 0),
	SD_BUS_PROPERTY("FanTargetGpu", "u", prop_fan_target_gpu, 0, 0),
	SD_BUS_PROPERTY("FanAuto", "b", prop_fan_auto, 0, 0),
	SD_BUS_PROPERTY("BatteryLimiter", "i", prop_battery_limiter, 0, 0),
	SD_BUS_PROPERTY("UsbCharging", "i", prop_usb_charging, 0, 0),
	SD_BUS_PROPERTY("Rgb", "s", prop_rgb, 0, 0),
	SD_BUS_PROPERTY("Logo", "s", prop_logo, 0, 0),

	SD_BUS_METHOD("SetFanTarget", "uu", NULL, method_set_fan_target,
		      SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetFanAuto", NULL, NULL, method_set_fan_auto,
		      SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetRgb", "s", NULL, method_set_rgb,
		      SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetLogo", "s", NULL, method_set_logo,
		      SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("Refresh", NULL, NULL, method_refresh,
		      SD_BUS_VTABLE_UNPRIVILEGED),

	SD_BUS_SIGNAL("ProfileChanged", "s", 0),

	SD_BUS_VTABLE_END,
};

int main(int argc, char **argv)
{
	sd_bus_slot *slot = NULL;
	int r;

	(void)argc;
	(void)argv;

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	r = sd_bus_open_system(&g_bus);
	if (r < 0) {
		fprintf(stderr, "perfectsensed: open system bus: %s\n",
			strerror(-r));
		return 1;
	}

	r = sd_bus_add_object_vtable(g_bus, &slot, PS_DBUS_PATH, PS_DBUS_IFACE,
				     sense1_vtable, NULL);
	if (r < 0) {
		fprintf(stderr, "perfectsensed: add vtable: %s\n", strerror(-r));
		goto out;
	}

	r = sd_bus_request_name(g_bus, PS_DBUS_NAME, 0);
	if (r < 0) {
		fprintf(stderr, "perfectsensed: request name: %s\n",
			strerror(-r));
		goto out;
	}

	if (ps_mode_get(g_last_profile, sizeof(g_last_profile)) < 0)
		g_last_profile[0] = '\0';

	sd_notify(0, "READY=1");
	fprintf(stderr, "perfectsensed: listening on %s\n", PS_DBUS_NAME);

	while (!g_stop) {
		r = sd_bus_process(g_bus, NULL);
		if (r < 0) {
			fprintf(stderr, "perfectsensed: process: %s\n",
				strerror(-r));
			break;
		}
		if (r > 0)
			continue;
		r = sd_bus_wait(g_bus, (uint64_t)-1);
		if (r < 0 && r != -EINTR) {
			fprintf(stderr, "perfectsensed: wait: %s\n",
				strerror(-r));
			break;
		}
	}

	sd_notify(0, "STOPPING=1");
	r = 0;

out:
	sd_bus_slot_unref(slot);
	sd_bus_unref(g_bus);
	return r < 0 ? 1 : 0;
}
