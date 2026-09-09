/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * perfectsensed — system daemon (no D-Bus)
 *
 * - Watches PerfectSense platform uevents (mode / turbo / AC)
 * - Runs FanAggressor timerfd only while offset mode is enabled
 * - SIGHUP reloads FanAggressor config from disk
 *
 * Control plane for apps/scripts: sysfs + /usr/bin/perfectsense (root).
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

#include <systemd/sd-daemon.h>
#include <systemd/sd-device.h>

static volatile sig_atomic_t g_stop;
static volatile sig_atomic_t g_reload;
static sd_device_monitor *g_udev_mon;
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

	r = fa_runtime_init();
	if (r < 0)
		fprintf(stderr, "perfectsensed: fan aggressor init: %s\n",
			strerror(-r));

	r = udev_monitor_setup();
	if (r < 0)
		fprintf(stderr, "perfectsensed: udev monitor: %s\n",
			strerror(-r));

	sd_notify(0, "READY=1");
	fprintf(stderr, "perfectsensed: running (fan_aggressor=%s, udev=%s)\n",
		fa_runtime_enabled() ? "on" : "off",
		g_udev_mon ? "on" : "off");

	while (!g_stop) {
		struct pollfd fds[2];
		int nfds = 0;
		int tfd;
		int ufd = -1;

		if (g_reload) {
			g_reload = 0;
			r = fa_runtime_reload();
			if (r < 0)
				fprintf(stderr,
					"perfectsensed: FA reload: %s\n",
					strerror(-r));
			else
				fprintf(stderr,
					"perfectsensed: FA reloaded (enabled=%s)\n",
					fa_runtime_enabled() ? "on" : "off");
		}

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

		if (nfds == 0) {
			/* Idle: wait for signal (FA off, no udev). */
			pause();
			continue;
		}

		r = poll(fds, (nfds_t)nfds, -1);
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
				fa_runtime_on_timer();
			else if (ufd >= 0 && fds[i].fd == ufd)
				on_udev_event();
		}
	}

	sd_notify(0, "STOPPING=1");
	fa_runtime_shutdown();
	if (g_udev_mon)
		sd_device_monitor_unref(g_udev_mon);
	return 0;
}
