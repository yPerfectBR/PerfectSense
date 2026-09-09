/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * perfectsensed fan curve runtime (timerfd only while enabled).
 */

#define _GNU_SOURCE

#include "fan_curve_runtime.h"
#include "perfectsense.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

static struct ps_fan_curve_config g_cfg;
static int g_timer_fd = -1;
static int g_owned;
static int g_paused_manual;
static unsigned g_last_cpu;
static unsigned g_last_gpu;
static int g_temp_cpu;
static int g_temp_gpu;

static int arm_timer(int interval_ms)
{
	struct itimerspec its;

	if (g_timer_fd < 0) {
		g_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
		if (g_timer_fd < 0)
			return -errno;
	}

	memset(&its, 0, sizeof(its));
	its.it_value.tv_sec = interval_ms / 1000;
	its.it_value.tv_nsec = (long)(interval_ms % 1000) * 1000000L;
	its.it_interval = its.it_value;
	if (timerfd_settime(g_timer_fd, 0, &its, NULL) < 0)
		return -errno;
	return 0;
}

static void disarm_timer(void)
{
	struct itimerspec its;

	if (g_timer_fd < 0)
		return;
	memset(&its, 0, sizeof(its));
	timerfd_settime(g_timer_fd, 0, &its, NULL);
	close(g_timer_fd);
	g_timer_fd = -1;
}

static void write_runtime_status(void)
{
	FILE *f;

	f = fopen("/run/perfectsense/fan-curve.status", "w");
	if (!f)
		return;
	fprintf(f,
		"enabled=%d active=%d paused=%d temp_cpu=%d temp_gpu=%d "
		"applied=%u,%u\n",
		g_cfg.enabled, g_owned && !g_paused_manual, g_paused_manual,
		g_temp_cpu, g_temp_gpu, g_last_cpu, g_last_gpu);
	fclose(f);
}

static int disengage(void)
{
	int err = ps_fan_set_auto();

	g_owned = 0;
	g_last_cpu = 0;
	g_last_gpu = 0;
	return err;
}

int fc_runtime_init(void)
{
	ps_fan_curve_config_load(&g_cfg);
	g_owned = 0;
	g_paused_manual = 0;
	g_timer_fd = -1;
	if (g_cfg.enabled)
		return arm_timer(g_cfg.interval_ms);
	return 0;
}

void fc_runtime_shutdown(void)
{
	if (g_owned)
		ps_fan_set_auto();
	disarm_timer();
	g_owned = 0;
}

int fc_runtime_timer_fd(void)
{
	return g_cfg.enabled ? g_timer_fd : -1;
}

int fc_runtime_enabled(void)
{
	return g_cfg.enabled;
}

void fc_runtime_get_status(struct fc_runtime_status *st)
{
	if (!st)
		return;
	memset(st, 0, sizeof(*st));
	st->enabled = g_cfg.enabled;
	st->active = g_owned && !g_paused_manual;
	st->paused_manual = g_paused_manual;
	st->temp_cpu = g_temp_cpu;
	st->temp_gpu = g_temp_gpu;
	st->applied_cpu = g_last_cpu;
	st->applied_gpu = g_last_gpu;
	st->cfg = g_cfg;
}

int fc_runtime_reload(void)
{
	int was = g_cfg.enabled;

	ps_fan_curve_config_load(&g_cfg);
	if (!g_cfg.enabled) {
		if (was || g_owned)
			disengage();
		disarm_timer();
		g_paused_manual = 0;
		write_runtime_status();
		return 0;
	}
	return arm_timer(g_cfg.interval_ms);
}

void fc_runtime_on_timer(void)
{
	uint64_t exp;
	unsigned target_cpu, target_gpu, apply_c, apply_g;
	int cpu_c = 0, gpu_c = 0;

	if (g_timer_fd < 0 || !g_cfg.enabled)
		return;
	if (read(g_timer_fd, &exp, sizeof(exp)) < 0)
		return;
	if (ps_available() != 0)
		return;

	/* Manual fan set by user → pause until back to auto or our targets */
	if (ps_fan_target_get(&target_cpu, &target_gpu) == 0) {
		if (!(target_cpu == 0 && target_gpu == 0) &&
		    !(g_owned && target_cpu == g_last_cpu &&
		      target_gpu == g_last_gpu)) {
			g_paused_manual = 1;
			g_owned = 0;
			write_runtime_status();
			return;
		}
	}
	g_paused_manual = 0;

	if (ps_temps_read(&cpu_c, &gpu_c) != 0)
		return;
	g_temp_cpu = cpu_c;
	g_temp_gpu = gpu_c;

	ps_fan_curve_eval(&g_cfg, cpu_c, gpu_c, &apply_c, &apply_g);
	/* Always re-apply: thermal profile changes can steal EC fan control */
	if (ps_fan_set(apply_c, apply_g) == 0) {
		g_last_cpu = apply_c;
		g_last_gpu = apply_g;
		g_owned = 1;
	}
	write_runtime_status();
}
