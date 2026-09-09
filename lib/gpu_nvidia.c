/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * NVIDIA PowerMizer Preferred Mode for Predator Helios (PHN16-72).
 *
 * Maps thermal profiles → GPUPowerMizerMode via nvidia-settings:
 *   low-power / quiet          → Normal (0)
 *   balanced                   → Auto (2)
 *   balanced-performance /
 *   performance                → Prefer Maximum Performance (1)
 *
 * Note: on pure Wayland, NV-CONTROL is often unavailable and the attribute
 * may not stick; we still apply best-effort and return an error if verify fails.
 */

#define _POSIX_C_SOURCE 200809L

#include "perfectsense.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* NVIDIA GPUPowerMizerMode values (nvidia-settings) */
#define PS_PM_NORMAL	0 /* UI: Normal / Adaptive */
#define PS_PM_MAX	1 /* UI: Prefer Maximum Performance */
#define PS_PM_AUTO	2 /* UI: Auto */

static int run_nvidia_settings(char *const argv[])
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		return -errno;
	if (pid == 0) {
		execvp("nvidia-settings", argv);
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

int ps_gpu_powermizer_get(int *mode)
{
	FILE *fp;
	char line[128];
	int v = -1;

	if (!mode)
		return -EINVAL;

	/*
	 * Prefer machine-readable -t; fall back to parsing full query.
	 * Runs in the caller's environment (DISPLAY / WAYLAND).
	 */
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

	err = run_nvidia_settings(argv);
	if (err)
		return err;

	/* Verify — on broken NV-CONTROL paths assign "succeeds" but stays put. */
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
