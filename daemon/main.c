/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * perfectsensed — system daemon (no D-Bus)
 *
 * - Watches PerfectSense platform uevents (mode / turbo / AC)
 * - Optional custom fan curve timerfd (when enabled)
 * - SIGHUP reloads fan-curve config from disk
 *
 * Control plane for apps/scripts: sysfs + /usr/bin/perfectsense (root).
 */

#define _POSIX_C_SOURCE 200809L

#include "perfectsense.h"
#include "fan_curve_runtime.h"

#include <errno.h>
#include <fcntl.h>
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

static volatile sig_atomic_t g_stop;
static volatile sig_atomic_t g_reload;
static sd_device_monitor *g_udev_mon;
static sd_bus *g_bus;
/* Delay inhibitor held until the pre-suspend snapshot is taken (-1 = none). */
static int g_sleep_lock = -1;
static struct ps_status g_snap;
static int g_snap_valid;
/* GPU OC profile wanted but the dGPU was asleep; retried on the housekeeping tick. */
static int g_oc_pending;
#define HOUSEKEEPING_MS 30000
static char g_last_profile[64];
/* Profile to restore after AC returns (empty if none). */
static char g_saved_ac_profile[64];

static const char *const g_mode_cycle[] = {
	"low-power",
	"quiet",
	"balanced",
	"balanced-performance",
	"performance",
};

static int profile_requires_ac(const char *name)
{
	if (!name || !name[0])
		return 0;
	return !strcmp(name, "quiet") ||
	       !strcmp(name, "balanced-performance") ||
	       !strcmp(name, "performance");
}

static void on_signal(int sig)
{
	if (sig == SIGHUP)
		g_reload = 1;
	else
		g_stop = 1;
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

	for (i = 1; i <= n; i++) {
		size_t idx = (start + i) % n;

		r = ps_mode_set(g_mode_cycle[idx]);
		if (r == 0 || r == -EBUSY) {
			snprintf(g_last_profile, sizeof(g_last_profile), "%s",
				 g_mode_cycle[idx]);
			fprintf(stderr, "perfectsensed: mode key → %s%s\n",
				g_last_profile,
				r == -EBUSY ? " (nvidia-powerd sync failed)"
					    : "");
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

	if (ps_cpu_power_read(&cpu) == 0 &&
	    !strcmp(cpu.governor, "performance") &&
	    !strcmp(st.thermal_profile, "performance")) {
		r = ps_cpu_power_restore();
		if (r < 0)
			fprintf(stderr, "perfectsensed: turbo restore: %s\n",
				strerror(-r));
		r = ps_mode_set("balanced");
		if (r == 0 || r == -EBUSY) {
			snprintf(g_last_profile, sizeof(g_last_profile),
				 "balanced");
			fprintf(stderr,
				"perfectsensed: turbo key → balanced%s\n",
				r == -EBUSY ? " (nvidia-powerd sync failed)"
					    : "");
		}
		return;
	}

	r = ps_cpu_power_preset("turbo", 1);
	if (r < 0) {
		fprintf(stderr, "perfectsensed: turbo key preset: %s\n",
			strerror(-r));
		return;
	}
	if (ps_mode_get(g_last_profile, sizeof(g_last_profile)) == 0)
		fprintf(stderr, "perfectsensed: turbo key → %s\n",
			g_last_profile);
}

static void handle_ac_offline(void)
{
	char cur[64];
	struct ps_cpu_power cpu;
	int r;

	if (ps_mode_get(cur, sizeof(cur)) < 0)
		snprintf(cur, sizeof(cur), "%s", g_last_profile);

	if (!profile_requires_ac(cur)) {
		(void)ps_gpu_powerd_for_profile(cur[0] ? cur : "balanced");
		return;
	}

	snprintf(g_saved_ac_profile, sizeof(g_saved_ac_profile), "%s", cur);
	fprintf(stderr, "perfectsensed: AC offline — demote %s → balanced\n",
		cur);

	memset(&cpu, 0, sizeof(cpu));
	if (ps_cpu_power_read(&cpu) == 0 &&
	    !strcmp(cpu.governor, "performance")) {
		r = ps_cpu_power_restore();
		if (r < 0)
			fprintf(stderr, "perfectsensed: AC demote CPU: %s\n",
				strerror(-r));
	}

	r = ps_mode_set("balanced");
	if (r < 0 && r != -EBUSY) {
		fprintf(stderr, "perfectsensed: AC demote mode: %s\n",
			strerror(-r));
		return;
	}
	if (r == -EBUSY)
		fprintf(stderr,
			"perfectsensed: AC demote: nvidia-powerd sync failed\n");

	snprintf(g_last_profile, sizeof(g_last_profile), "balanced");
}

static void handle_ac_online(void)
{
	int r;

	if (!g_saved_ac_profile[0]) {
		if (g_last_profile[0])
			(void)ps_gpu_powerd_for_profile(g_last_profile);
		return;
	}

	fprintf(stderr, "perfectsensed: AC online — restore %s\n",
		g_saved_ac_profile);
	r = ps_mode_set(g_saved_ac_profile);
	if (r == 0 || r == -EBUSY) {
		snprintf(g_last_profile, sizeof(g_last_profile), "%s",
			 g_saved_ac_profile);
		if (r == -EBUSY)
			fprintf(stderr,
				"perfectsensed: AC restore: nvidia-powerd sync failed\n");
		g_saved_ac_profile[0] = '\0';
		return;
	}

	fprintf(stderr, "perfectsensed: AC restore %s: %s\n",
		g_saved_ac_profile, strerror(-r));
	g_saved_ac_profile[0] = '\0';
}

static void on_udev_event(void)
{
	sd_device *dev = NULL;
	const char *sysname = NULL;
	const char *event = NULL;
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

	fprintf(stderr, "perfectsensed: event %s\n", event);

	if (!strcmp(event, "mode"))
		handle_mode_key();
	else if (!strcmp(event, "turbo"))
		handle_turbo_key();
	else if (!strcmp(event, "ac-offline"))
		handle_ac_offline();
	else if (!strcmp(event, "ac-online"))
		handle_ac_online();

out:
	sd_device_unref(dev);
}


/* ---------- suspend/resume (logind PrepareForSleep) ---------- */

static void take_sleep_lock(void);

static void oc_try_apply(void)
{
	char cur[64];
	int r;

	if (ps_mode_get(cur, sizeof(cur)) < 0)
		return;
	r = ps_gpu_oc_apply(cur);
	if (r == -EAGAIN) {
		g_oc_pending = 1;
		return;
	}
	g_oc_pending = 0;
	if (r < 0 && r != -ENODEV && r != -ENOENT && r != -ENOSYS)
		fprintf(stderr, "perfectsensed: gpu oc %s: %s\n", cur, strerror(-r));
}

static void snapshot_state(void)
{
	memset(&g_snap, 0, sizeof(g_snap));
	g_snap_valid = ps_status_read(&g_snap) == 0;
}

static void restore_int(const char *attr, int want, int have)
{
	if (want < 0 || want == have)
		return;
	if (ps_attr_set_int(attr, want) < 0)
		fprintf(stderr, "perfectsensed: resume: could not restore %s\n", attr);
}

/* The EC/firmware can forget runtime settings across suspend: put back anything that differs. */
static void restore_after_resume(void)
{
	struct ps_status now;

	if (!g_snap_valid)
		return;
	memset(&now, 0, sizeof(now));
	if (ps_status_read(&now) < 0)
		return;

	if (g_snap.thermal_profile[0] && strcmp(g_snap.thermal_profile, now.thermal_profile)) {
		fprintf(stderr, "perfectsensed: resume: profile %s → %s\n",
			now.thermal_profile, g_snap.thermal_profile);
		(void)ps_mode_set(g_snap.thermal_profile);
	}
	if (!g_snap.fan_auto && !fc_runtime_enabled() &&
	    (g_snap.fan_target_cpu != now.fan_target_cpu || g_snap.fan_target_gpu != now.fan_target_gpu ||
	     now.fan_auto)) {
		fprintf(stderr, "perfectsensed: resume: fans %u/%u%%\n", g_snap.fan_target_cpu,
			g_snap.fan_target_gpu);
		(void)ps_fan_set(g_snap.fan_target_cpu, g_snap.fan_target_gpu);
	}
	if (g_snap.rgb[0] && strcmp(g_snap.rgb, now.rgb))
		(void)ps_rgb_set(g_snap.rgb);
	if (g_snap.rgb_effect[0] && strcmp(g_snap.rgb_effect, now.rgb_effect))
		(void)ps_rgb_effect_set(g_snap.rgb_effect);
	if (g_snap.logo[0] && strcmp(g_snap.logo, now.logo))
		(void)ps_logo_set(g_snap.logo);
	restore_int("backlight_timeout", g_snap.backlight_timeout, now.backlight_timeout);
	restore_int("usb_charging", g_snap.usb_charging, now.usb_charging);
	restore_int("battery_limiter", g_snap.battery_limiter, now.battery_limiter);
	restore_int("lcd_override", g_snap.lcd_override, now.lcd_override);

	/* A GPU power cycle drops clock offsets: re-apply the profile when it wakes up. */
	g_oc_pending = 1;
	oc_try_apply();
}

static int on_prepare_for_sleep(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
	int going_down = 0;

	(void)userdata;
	(void)err;
	if (sd_bus_message_read(m, "b", &going_down) < 0)
		return 0;

	if (going_down) {
		snapshot_state();
		if (g_sleep_lock >= 0) {
			close(g_sleep_lock);
			g_sleep_lock = -1;
		}
		return 0;
	}

	/* Give the EC a moment to settle, then reconcile. */
	{
		struct timespec ts = { .tv_sec = 2, .tv_nsec = 0 };

		nanosleep(&ts, NULL);
	}
	fprintf(stderr, "perfectsensed: resume detected — reconciling settings\n");
	restore_after_resume();
	take_sleep_lock();
	return 0;
}

static void take_sleep_lock(void)
{
	sd_bus_message *reply = NULL;
	int fd = -1;

	if (!g_bus || g_sleep_lock >= 0)
		return;
	if (sd_bus_call_method(g_bus, "org.freedesktop.login1", "/org/freedesktop/login1",
			       "org.freedesktop.login1.Manager", "Inhibit", NULL, &reply, "ssss",
			       "sleep", "perfectsensed", "Snapshot hardware state before suspend",
			       "delay") < 0)
		return;
	if (sd_bus_message_read(reply, "h", &fd) >= 0 && fd >= 0)
		g_sleep_lock = fcntl(fd, F_DUPFD_CLOEXEC, 3);
	sd_bus_message_unref(reply);
}

static int bus_setup(void)
{
	int r = sd_bus_open_system(&g_bus);

	if (r < 0)
		return r;
	r = sd_bus_match_signal(g_bus, NULL, "org.freedesktop.login1", "/org/freedesktop/login1",
				"org.freedesktop.login1.Manager", "PrepareForSleep",
				on_prepare_for_sleep, NULL);
	if (r < 0) {
		sd_bus_unref(g_bus);
		g_bus = NULL;
		return r;
	}
	take_sleep_lock();
	return 0;
}

static void housekeeping(void)
{
	if (g_oc_pending)
		oc_try_apply();
	/* The delay lock is released on each suspend; re-arm it for the next one. */
	take_sleep_lock();
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

	r = sd_device_monitor_start(g_udev_mon, NULL, NULL);
	if (r < 0)
		goto fail;

	return 0;

fail:
	sd_device_monitor_unref(g_udev_mon);
	g_udev_mon = NULL;
	return r;
}

int main(int argc, char **argv)
{
	struct sigaction sa;
	int r;

	(void)argc;
	(void)argv;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	if (ps_mode_get(g_last_profile, sizeof(g_last_profile)) < 0)
		g_last_profile[0] = '\0';
	else
		(void)ps_gpu_powerd_for_profile(g_last_profile);

	{
		struct ps_status st;

		memset(&st, 0, sizeof(st));
		if (ps_status_read(&st) == 0 && !st.ac_online &&
		    profile_requires_ac(g_last_profile))
			handle_ac_offline();
	}

	r = fc_runtime_init();
	if (r < 0)
		fprintf(stderr, "perfectsensed: fan curve init: %s\n",
			strerror(-r));

	r = udev_monitor_setup();
	if (r < 0)
		fprintf(stderr, "perfectsensed: udev monitor: %s\n",
			strerror(-r));

	r = bus_setup();
	if (r < 0)
		fprintf(stderr, "perfectsensed: logind bus (no resume restore): %s\n",
			strerror(-r));
	snapshot_state();
	oc_try_apply();

	sd_notify(0, "READY=1");
	fprintf(stderr, "perfectsensed: running (fan_curve=%s, udev=%s, resume=%s)\n",
		fc_runtime_enabled() ? "on" : "off",
		g_udev_mon ? "on" : "off", g_bus ? "on" : "off");

	while (!g_stop) {
		struct pollfd fds[3];
		int nfds = 0;
		int tfd;
		int ufd = -1;
		int bfd = -1;
		int timeout_ms = HOUSEKEEPING_MS;

		if (g_reload) {
			g_reload = 0;
			r = fc_runtime_reload();
			if (r < 0)
				fprintf(stderr,
					"perfectsensed: fan curve reload: %s\n",
					strerror(-r));
			else
				fprintf(stderr,
					"perfectsensed: fan curve reloaded (enabled=%s)\n",
					fc_runtime_enabled() ? "on" : "off");
		}

		tfd = fc_runtime_timer_fd();
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

		if (g_bus) {
			uint64_t usec = UINT64_MAX;

			bfd = sd_bus_get_fd(g_bus);
			if (bfd >= 0) {
				fds[nfds].fd = bfd;
				fds[nfds].events = (short)sd_bus_get_events(g_bus);
				fds[nfds].revents = 0;
				nfds++;
			}
			if (sd_bus_get_timeout(g_bus, &usec) >= 0 && usec != UINT64_MAX) {
				uint64_t now_us = (uint64_t)0;
				struct timespec ts;

				clock_gettime(CLOCK_MONOTONIC, &ts);
				now_us = (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
				if (usec <= now_us)
					timeout_ms = 0;
				else if ((usec - now_us) / 1000u < (uint64_t)timeout_ms)
					timeout_ms = (int)((usec - now_us) / 1000u) + 1;
			}
		}

		r = poll(fds, (nfds_t)nfds, timeout_ms);
		if (r == 0)
			housekeeping();
		if (r < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "perfectsensed: poll: %s\n",
				strerror(errno));
			break;
		}

		for (int i = 0; i < nfds; i++) {
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
				fc_runtime_on_timer();
			else if (ufd >= 0 && fds[i].fd == ufd)
				on_udev_event();
		}
		if (g_bus) {
			while (sd_bus_process(g_bus, NULL) > 0)
				;
		}
	}

	sd_notify(0, "STOPPING=1");
	fc_runtime_shutdown();
	if (g_udev_mon)
		sd_device_monitor_unref(g_udev_mon);
	if (g_sleep_lock >= 0)
		close(g_sleep_lock);
	if (g_bus)
		sd_bus_unref(g_bus);
	return 0;
}
