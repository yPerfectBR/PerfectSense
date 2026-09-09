/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * NVIDIA GPU helpers for Predator Helios PHN16-72.
 *
 * Primary policy (nekro/README guidance): nvidia-powerd Dynamic Boost.
 *   low-power / quiet → stop  nvidia-powerd  (~80W firmware default)
 *   balanced and up   → start nvidia-powerd  (up to ~140W when needed)
 *
 * PowerMizer Preferred Mode remains available via CLI but is best-effort on
 * Wayland (NV-CONTROL often does not stick).
 */

#define _POSIX_C_SOURCE 200809L

#include "perfectsense.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define PS_POWERD_UNIT "nvidia-powerd.service"

/* NVIDIA GPUPowerMizerMode values (nvidia-settings) */
#define PS_PM_NORMAL 0
#define PS_PM_MAX    1
#define PS_PM_AUTO   2

static int run_cmd(char *const argv[])
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		return -errno;
	if (pid == 0) {
		execvp(argv[0], argv);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -errno;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return 0;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
		return -ENOENT;
	return -EIO;
}

/* ---------- nvidia-powerd (Dynamic Boost / TGP headroom) ---------- */

int ps_gpu_powerd_get(int *enabled)
{
	char *argv[] = { "systemctl", "is-active", "--quiet", PS_POWERD_UNIT,
			 NULL };
	int err;

	if (!enabled)
		return -EINVAL;

	err = run_cmd(argv);
	if (err == 0) {
		*enabled = 1;
		return 0;
	}
	/* inactive / failed / missing → treat as off when unit exists */
	if (err == -EIO || err == -ENOENT) {
		*enabled = 0;
		return 0;
	}
	return err;
}

int ps_gpu_powerd_set(int enabled)
{
	char *argv_start[] = { "systemctl", "start", PS_POWERD_UNIT, NULL };
	char *argv_stop[] = { "systemctl", "stop", PS_POWERD_UNIT, NULL };
	char *argv_reset[] = { "systemctl", "reset-failed", PS_POWERD_UNIT,
			       NULL };
	int err;
	int cur = -1;

	err = ps_gpu_powerd_get(&cur);
	if (err)
		return err;
	/* Already in the desired state — avoid systemd start-limit thrash. */
	if ((cur != 0) == (enabled != 0))
		return 0;

	err = run_cmd(enabled ? argv_start : argv_stop);
	if (err && enabled) {
		/* Unit may be in failed/start-limit; clear once and retry. */
		(void)run_cmd(argv_reset);
		err = run_cmd(argv_start);
	}
	if (err)
		return err;

	err = ps_gpu_powerd_get(&cur);
	if (err)
		return err;
	if ((cur != 0) != (enabled != 0))
		return -EBUSY;
	return 0;
}

int ps_gpu_powerd_wanted(const char *thermal_name, int *wanted)
{
	const char *n;

	if (!wanted || !thermal_name || !*thermal_name)
		return -EINVAL;

	n = thermal_name;
	if (!strcmp(n, "eco"))
		n = "low-power";
	else if (!strcmp(n, "turbo"))
		n = "performance";
	else if (!strcmp(n, "bp") || !strcmp(n, "performance-balanced"))
		n = "balanced-performance";

	if (!strcmp(n, "low-power") || !strcmp(n, "quiet"))
		*wanted = 0;
	else if (!strcmp(n, "balanced") ||
		 !strcmp(n, "balanced-performance") ||
		 !strcmp(n, "performance"))
		*wanted = 1;
	else
		return -EINVAL;
	return 0;
}

int ps_gpu_powerd_for_profile(const char *thermal_name)
{
	int enable;
	int err;

	err = ps_gpu_powerd_wanted(thermal_name, &enable);
	if (err)
		return err;
	return ps_gpu_powerd_set(enable);
}

int ps_gpu_powerd_synced(const char *thermal_name, int *synced, int *enabled,
			 int *wanted)
{
	char profile[64];
	int on = 0, want = 0;
	int err;

	if (!synced)
		return -EINVAL;

	if (thermal_name && *thermal_name) {
		snprintf(profile, sizeof(profile), "%s", thermal_name);
	} else {
		err = ps_mode_get(profile, sizeof(profile));
		if (err)
			return err;
	}

	err = ps_gpu_powerd_wanted(profile, &want);
	if (err)
		return err;
	err = ps_gpu_powerd_get(&on);
	if (err)
		return err;

	*synced = ((on != 0) == (want != 0)) ? 1 : 0;
	if (enabled)
		*enabled = on ? 1 : 0;
	if (wanted)
		*wanted = want ? 1 : 0;
	return 0;
}

/* ---------- PowerMizer Preferred Mode (optional / X11-ish) ---------- */

int ps_gpu_powermizer_get(int *mode)
{
	FILE *fp;
	char line[128];
	int v = -1;

	if (!mode)
		return -EINVAL;

	fp = popen("nvidia-settings -q GpuPowerMizerMode -t 2>/dev/null", "r");
	if (!fp)
		return -errno;
	if (fgets(line, sizeof(line), fp))
		v = atoi(line);
	pclose(fp);

	if (v < 0 || v > 2)
		return -ENODEV;
	*mode = v;
	return 0;
}

int ps_gpu_powermizer_set(int mode)
{
	char val[8];
	char attr[64];
	char *argv[5];
	int got = -1;
	int err;

	if (mode < 0 || mode > 2)
		return -EINVAL;

	snprintf(val, sizeof(val), "%d", mode);
	snprintf(attr, sizeof(attr), "[gpu:0]/GPUPowerMizerMode=%s", val);
	argv[0] = "nvidia-settings";
	argv[1] = "-a";
	argv[2] = attr;
	argv[3] = NULL;

	err = run_cmd(argv);
	if (err)
		return err;

	err = ps_gpu_powermizer_get(&got);
	if (err)
		return err;
	if (got != mode)
		return -EAGAIN;
	return 0;
}

int ps_gpu_powermizer_for_profile(const char *thermal_name)
{
	int mode = PS_PM_AUTO;

	if (!thermal_name || !*thermal_name)
		return -EINVAL;

	if (!strcmp(thermal_name, "low-power") ||
	    !strcmp(thermal_name, "quiet") || !strcmp(thermal_name, "eco"))
		mode = PS_PM_NORMAL;
	else if (!strcmp(thermal_name, "balanced"))
		mode = PS_PM_AUTO;
	else if (!strcmp(thermal_name, "balanced-performance") ||
		 !strcmp(thermal_name, "performance") ||
		 !strcmp(thermal_name, "bp") || !strcmp(thermal_name, "turbo"))
		mode = PS_PM_MAX;
	else
		return -EINVAL;

	return ps_gpu_powermizer_set(mode);
}

const char *ps_gpu_powermizer_name(int mode)
{
	switch (mode) {
	case PS_PM_NORMAL:
		return "normal";
	case PS_PM_MAX:
		return "max";
	case PS_PM_AUTO:
		return "auto";
	default:
		return "unknown";
	}
}

int ps_gpu_powermizer_from_name(const char *name, int *mode)
{
	if (!name || !mode)
		return -EINVAL;
	if (!strcmp(name, "normal") || !strcmp(name, "adaptive"))
		*mode = PS_PM_NORMAL;
	else if (!strcmp(name, "max") || !strcmp(name, "maximum") ||
		 !strcmp(name, "prefer-max"))
		*mode = PS_PM_MAX;
	else if (!strcmp(name, "auto"))
		*mode = PS_PM_AUTO;
	else
		return -EINVAL;
	return 0;
}
