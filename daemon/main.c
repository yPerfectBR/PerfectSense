/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * perfectsensed — system D-Bus daemon for org.perfectsense.Sense1
 *
 * Talks to PerfectSense sysfs via libperfectsense.
 * FanAggressor timerfd runs only while offset mode is enabled.
 * Phase 10: sd-device watches platform uevents → Hotkey D-Bus signal.
 */

#define _POSIX_C_SOURCE 200809L

#include "perfectsense.h"
#include "fan_aggressor_runtime.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>
#include <systemd/sd-device.h>

#define PS_DBUS_NAME  "org.perfectsense.Sense1"
#define PS_DBUS_PATH  "/org/perfectsense/Sense1"
#define PS_DBUS_IFACE "org.perfectsense.Sense1"

static volatile sig_atomic_t g_stop;
static sd_bus *g_bus;
static sd_device_monitor *g_udev_mon;
static char g_last_profile[64];

static const char *const g_mode_cycle[] = {
	"low-power",
	"quiet",
	"balanced",
	"balanced-performance",
	"performance",
};

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

static int emit_hotkey(const char *event, unsigned key)
{
	return sd_bus_emit_signal(g_bus, PS_DBUS_PATH, PS_DBUS_IFACE, "Hotkey",
				  "su", event, key);
}

static void handle_mode_key(void)
{
	static struct timespec last;
	struct timespec now;
	char cur[64];
	size_t i, start = 0;
	size_t n = sizeof(g_mode_cycle) / sizeof(g_mode_cycle[0]);
	int r;

	clock_gettime(CLOCK_MONOTONIC, &now);
	if (last.tv_sec &&
	    ((now.tv_sec - last.tv_sec) * 1000L +
	     (now.tv_nsec - last.tv_nsec) / 1000000L) < 300)
		return;
	last = now;

	if (ps_mode_get(cur, sizeof(cur)) < 0)
		snprintf(cur, sizeof(cur), "balanced");

	for (i = 0; i < n; i++) {
		if (!strcmp(cur, g_mode_cycle[i])) {
			start = i;
			break;
		}
	}

	/* Skip profiles rejected by firmware (e.g. aggressive modes on battery). */
	for (i = 1; i <= n; i++) {
		size_t idx = (start + i) % n;

		r = ps_mode_set(g_mode_cycle[idx]);
		if (r == 0) {
			snprintf(g_last_profile, sizeof(g_last_profile), "%s",
				 g_mode_cycle[idx]);
			(void)emit_profile_changed(g_last_profile);
			sd_bus_emit_properties_changed(
				g_bus, PS_DBUS_PATH, PS_DBUS_IFACE,
				"ThermalProfile", "PlatformProfile", NULL);
			return;
		}
		if (r != -EOPNOTSUPP)
			fprintf(stderr, "perfectsensed: mode key set %s: %s\n",
				g_mode_cycle[idx], strerror(-r));
	}
	fprintf(stderr, "perfectsensed: mode key: no applicable profile\n");
}

static void handle_turbo_key(void)
{
	static struct timespec last;
	struct timespec now;
	struct ps_status st;
	struct ps_cpu_power cpu;
	int r;

	clock_gettime(CLOCK_MONOTONIC, &now);
	if (last.tv_sec &&
	    ((now.tv_sec - last.tv_sec) * 1000L +
	     (now.tv_nsec - last.tv_nsec) / 1000000L) < 300)
		return;
	last = now;

	memset(&st, 0, sizeof(st));
	memset(&cpu, 0, sizeof(cpu));

	if (ps_status_read(&st) == 0 && !st.ac_online) {
		fprintf(stderr,
			"perfectsensed: turbo key ignored (AC required)\n");
		return;
	}

	/*
	 * Toggle: if already on performance + performance governor, restore
	 * the CPU snapshot and drop thermal to balanced.
	 */
	if (ps_cpu_power_read(&cpu) == 0 &&
	    !strcmp(cpu.governor, "performance") &&
	    !strcmp(st.thermal_profile, "performance")) {
		r = ps_cpu_power_restore();
		if (r < 0)
			fprintf(stderr, "perfectsensed: turbo restore: %s\n",
				strerror(-r));
		if (ps_mode_set("balanced") == 0) {
			snprintf(g_last_profile, sizeof(g_last_profile),
				 "balanced");
			(void)emit_profile_changed(g_last_profile);
		}
		sd_bus_emit_properties_changed(
			g_bus, PS_DBUS_PATH, PS_DBUS_IFACE, "ThermalProfile",
			"PlatformProfile", "CpuGovernor", "CpuEpp", "CpuTurbo",
			"CpuMaxPerfPct", "CpuPl1Uw", "CpuPl2Uw", NULL);
		return;
	}

	r = ps_cpu_power_preset("turbo", 1);
	if (r < 0) {
		fprintf(stderr, "perfectsensed: turbo key preset: %s\n",
			strerror(-r));
		return;
	}
	if (ps_mode_get(g_last_profile, sizeof(g_last_profile)) == 0)
		(void)emit_profile_changed(g_last_profile);
	sd_bus_emit_properties_changed(
		g_bus, PS_DBUS_PATH, PS_DBUS_IFACE, "ThermalProfile",
		"PlatformProfile", "CpuGovernor", "CpuEpp", "CpuTurbo",
		"CpuMaxPerfPct", "CpuPl1Uw", "CpuPl2Uw", NULL);
}

static void on_udev_event(void)
{
	sd_device *dev = NULL;
	const char *sysname = NULL;
	const char *event = NULL;
	const char *key_s = NULL;
	unsigned key = 0;
	int r;

	r = sd_device_monitor_receive(g_udev_mon, &dev);
	if (r < 0)
		return;

	(void)sd_device_get_sysname(dev, &sysname);
	if (!sysname || strcmp(sysname, "perfectsense") != 0)
		goto out;

	if (sd_device_get_property_value(dev, "PERFECTSENSE_EVENT", &event) < 0 ||
	    !event)
		goto out;

	if (sd_device_get_property_value(dev, "PERFECTSENSE_KEY", &key_s) == 0 &&
	    key_s)
		key = (unsigned)strtoul(key_s, NULL, 10);

	(void)emit_hotkey(event, key);

	if (!strcmp(event, "mode"))
		handle_mode_key();
	else if (!strcmp(event, "turbo"))
		handle_turbo_key();
	else if (!strcmp(event, "ac-online") || !strcmp(event, "ac-offline"))
		sd_bus_emit_properties_changed(g_bus, PS_DBUS_PATH, PS_DBUS_IFACE,
						"AcOnline", NULL);

out:
	sd_device_unref(dev);
}

static int udev_monitor_setup(void)
{
	int r;

	r = sd_device_monitor_new(&g_udev_mon);
	if (r < 0)
		return r;

	r = sd_device_monitor_filter_add_match_subsystem_devtype(g_udev_mon,
								"platform",
								NULL);
	if (r < 0)
		goto fail;

	r = sd_device_monitor_filter_update(g_udev_mon);
	if (r < 0)
		goto fail;

	/* NULL callback: retrieve devices via sd_device_monitor_receive(). */
	r = sd_device_monitor_start(g_udev_mon, NULL, NULL);
	if (r < 0)
		goto fail;

	return 0;

fail:
	sd_device_monitor_unref(g_udev_mon);
	g_udev_mon = NULL;
	return r;
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
		"UsbCharging", "Rgb", "Logo", "ApiVersion", "Available",
		"CpuGovernor", "CpuEpp", "CpuTurbo", "CpuMaxPerfPct",
		"CpuPl1Uw", "CpuPl2Uw", NULL);
	return sd_bus_reply_method_return(m, NULL);
}

static int prop_cpu_governor(sd_bus *bus, const char *path, const char *iface,
			     const char *prop, sd_bus_message *reply,
			     void *userdata, sd_bus_error *err)
{
	struct ps_cpu_power st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = ps_cpu_power_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "s", st.governor);
}

static int prop_cpu_epp(sd_bus *bus, const char *path, const char *iface,
			const char *prop, sd_bus_message *reply, void *userdata,
			sd_bus_error *err)
{
	struct ps_cpu_power st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = ps_cpu_power_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "s", st.epp);
}

static int prop_cpu_turbo(sd_bus *bus, const char *path, const char *iface,
			  const char *prop, sd_bus_message *reply,
			  void *userdata, sd_bus_error *err)
{
	struct ps_cpu_power st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = ps_cpu_power_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "b", st.no_turbo == 0);
}

static int prop_cpu_max_perf(sd_bus *bus, const char *path, const char *iface,
			     const char *prop, sd_bus_message *reply,
			     void *userdata, sd_bus_error *err)
{
	struct ps_cpu_power st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = ps_cpu_power_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "u", st.max_perf_pct);
}

static int prop_cpu_pl1(sd_bus *bus, const char *path, const char *iface,
			const char *prop, sd_bus_message *reply, void *userdata,
			sd_bus_error *err)
{
	struct ps_cpu_power st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = ps_cpu_power_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "t", (uint64_t)st.pl1_uw);
}

static int prop_cpu_pl2(sd_bus *bus, const char *path, const char *iface,
			const char *prop, sd_bus_message *reply, void *userdata,
			sd_bus_error *err)
{
	struct ps_cpu_power st;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = ps_cpu_power_read(&st);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	return sd_bus_message_append(reply, "t", (uint64_t)st.pl2_uw);
}

static int method_cpu_preset(sd_bus_message *m, void *userdata,
			     sd_bus_error *err)
{
	const char *name = NULL;
	int sync = 0;
	int r;

	(void)userdata;
	r = sd_bus_message_read(m, "sb", &name, &sync);
	if (r < 0)
		return r;
	r = ps_cpu_power_preset(name, sync);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	sd_bus_emit_properties_changed(
		g_bus, PS_DBUS_PATH, PS_DBUS_IFACE, "CpuGovernor", "CpuEpp",
		"CpuTurbo", "CpuMaxPerfPct", "CpuPl1Uw", "CpuPl2Uw",
		"ThermalProfile", "PlatformProfile", NULL);
	return sd_bus_reply_method_return(m, NULL);
}

static int method_cpu_restore(sd_bus_message *m, void *userdata,
			      sd_bus_error *err)
{
	int r;

	(void)userdata;
	r = ps_cpu_power_restore();
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	sd_bus_emit_properties_changed(
		g_bus, PS_DBUS_PATH, PS_DBUS_IFACE, "CpuGovernor", "CpuEpp",
		"CpuTurbo", "CpuMaxPerfPct", "CpuPl1Uw", "CpuPl2Uw", NULL);
	return sd_bus_reply_method_return(m, NULL);
}

static int prop_fa_enabled(sd_bus *bus, const char *path, const char *iface,
			   const char *prop, sd_bus_message *reply,
			   void *userdata, sd_bus_error *err)
{
	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	(void)err;
	return sd_bus_message_append(reply, "b", fa_runtime_enabled());
}

static int prop_fa_enabled_set(sd_bus *bus, const char *path, const char *iface,
			       const char *prop, sd_bus_message *value,
			       void *userdata, sd_bus_error *err)
{
	int on = 0;
	int r;

	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	r = sd_bus_message_read(value, "b", &on);
	if (r < 0)
		return r;
	r = fa_runtime_set_enabled(on);
	if (r < 0)
		return sd_bus_error_set_errno(err, -r);
	sd_bus_emit_signal(g_bus, PS_DBUS_PATH, PS_DBUS_IFACE,
			   "FanAggressorChanged", "b", on);
	return 1;
}

static int prop_fa_active(sd_bus *bus, const char *path, const char *iface,
			  const char *prop, sd_bus_message *reply,
			  void *userdata, sd_bus_error *err)
{
	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	(void)err;
	return sd_bus_message_append(reply, "b", fa_runtime_active());
}

static int method_fa_status(sd_bus_message *m, void *userdata,
			    sd_bus_error *err)
{
	struct fa_runtime_status st;

	(void)userdata;
	(void)err;
	fa_runtime_get_status(&st);
	return sd_bus_reply_method_return(
		m, "bbbiiuuuu", st.enabled, st.active, st.paused_manual,
		st.temp_cpu, st.temp_gpu, st.native_cpu, st.native_gpu,
		st.applied_cpu, st.applied_gpu);
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
	SD_BUS_PROPERTY("CpuGovernor", "s", prop_cpu_governor, 0, 0),
	SD_BUS_PROPERTY("CpuEpp", "s", prop_cpu_epp, 0, 0),
	SD_BUS_PROPERTY("CpuTurbo", "b", prop_cpu_turbo, 0, 0),
	SD_BUS_PROPERTY("CpuMaxPerfPct", "u", prop_cpu_max_perf, 0, 0),
	SD_BUS_PROPERTY("CpuPl1Uw", "t", prop_cpu_pl1, 0, 0),
	SD_BUS_PROPERTY("CpuPl2Uw", "t", prop_cpu_pl2, 0, 0),

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
	SD_BUS_METHOD("CpuPreset", "sb", NULL, method_cpu_preset,
		      SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("CpuRestore", NULL, NULL, method_cpu_restore,
		      SD_BUS_VTABLE_UNPRIVILEGED),

	SD_BUS_WRITABLE_PROPERTY("FanAggressorEnabled", "b", prop_fa_enabled,
				 prop_fa_enabled_set, 0,
				 SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_PROPERTY("FanAggressorActive", "b", prop_fa_active, 0, 0),
	SD_BUS_METHOD("FanAggressorStatus", NULL, "bbbiiuuuu", method_fa_status,
		      SD_BUS_VTABLE_UNPRIVILEGED),

	SD_BUS_SIGNAL("ProfileChanged", "s", 0),
	SD_BUS_SIGNAL("FanAggressorChanged", "b", 0),
	SD_BUS_SIGNAL("Hotkey", "su", 0),

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
	else
		(void)ps_gpu_powerd_for_profile(g_last_profile);

	r = fa_runtime_init();
	if (r < 0)
		fprintf(stderr, "perfectsensed: fan aggressor init: %s\n",
			strerror(-r));

	r = udev_monitor_setup();
	if (r < 0)
		fprintf(stderr, "perfectsensed: udev monitor: %s\n",
			strerror(-r));

	sd_notify(0, "READY=1");
	fprintf(stderr, "perfectsensed: listening on %s (fan_aggressor=%s, udev=%s)\n",
		PS_DBUS_NAME, fa_runtime_enabled() ? "on" : "off",
		g_udev_mon ? "on" : "off");

	while (!g_stop) {
		struct pollfd fds[3];
		int nfds = 1;
		int tfd;
		int ufd = -1;

		r = sd_bus_process(g_bus, NULL);
		if (r < 0) {
			fprintf(stderr, "perfectsensed: process: %s\n",
				strerror(-r));
			break;
		}
		if (r > 0)
			continue;

		fds[0].fd = sd_bus_get_fd(g_bus);
		fds[0].events = POLLIN;
		fds[0].revents = 0;
		tfd = fa_runtime_timer_fd();
		if (tfd >= 0) {
			fds[nfds].fd = tfd;
			fds[nfds].events = POLLIN;
			fds[nfds].revents = 0;
			nfds++;
		}
		if (g_udev_mon) {
			ufd = sd_device_monitor_get_fd(g_udev_mon);
			if (ufd >= 0) {
				fds[nfds].fd = ufd;
				fds[nfds].events = POLLIN;
				fds[nfds].revents = 0;
				nfds++;
			}
		}

		r = poll(fds, (nfds_t)nfds, -1);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "perfectsensed: poll: %s\n",
				strerror(errno));
			break;
		}
		if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			fprintf(stderr, "perfectsensed: D-Bus fd closed\n");
			break;
		}
		for (int i = 1; i < nfds; i++) {
			if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				fprintf(stderr,
					"perfectsensed: fd %d error; stopping\n",
					fds[i].fd);
				g_stop = 1;
				break;
			}
			if (!(fds[i].revents & POLLIN))
				continue;
			if (tfd >= 0 && fds[i].fd == tfd)
				fa_runtime_on_timer();
			else if (ufd >= 0 && fds[i].fd == ufd)
				on_udev_event();
		}
	}

	sd_notify(0, "STOPPING=1");
	fa_runtime_shutdown();
	r = 0;

out:
	if (g_udev_mon)
		sd_device_monitor_unref(g_udev_mon);
	sd_bus_slot_unref(slot);
	sd_bus_unref(g_bus);
	return r < 0 ? 1 : 0;
}
