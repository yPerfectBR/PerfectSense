/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * perfectsensed FanAggressor runtime (timerfd only while enabled).
 */

#define _GNU_SOURCE

#include "fan_aggressor_runtime.h"
#include "perfectsense.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

static struct ps_fan_aggressor_config g_cfg;
static int g_timer_fd = -1;
static int g_active;
static int g_owned;
static int g_paused_manual;
static time_t g_hot_since;
static unsigned g_native_cpu;
static unsigned g_native_gpu;
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

static int disengage(void)
{
	int err = ps_fan_set_auto();

	g_active = 0;
	g_owned = 0;
	g_hot_since = 0;
	g_last_cpu = 0;
	g_last_gpu = 0;
	return err;
}

int fa_runtime_init(void)
{
	ps_fan_aggressor_config_load(&g_cfg);
	g_active = 0;
	g_owned = 0;
	g_paused_manual = 0;
	g_hot_since = 0;
	g_timer_fd = -1;
	if (g_cfg.enabled)
		return arm_timer(g_cfg.interval_ms);
	return 0;
}

void fa_runtime_shutdown(void)
{
	if (g_owned)
		ps_fan_set_auto();
	disarm_timer();
	g_active = 0;
	g_owned = 0;
}

int fa_runtime_timer_fd(void)
{
	return g_cfg.enabled ? g_timer_fd : -1;
}

int fa_runtime_enabled(void)
{
	return g_cfg.enabled;
}

int fa_runtime_active(void)
{
	return g_active;
}

void fa_runtime_get_config(struct ps_fan_aggressor_config *cfg)
{
	if (cfg)
		*cfg = g_cfg;
}

void fa_runtime_get_status(struct fa_runtime_status *st)
{
	if (!st)
		return;
	memset(st, 0, sizeof(*st));
	st->enabled = g_cfg.enabled;
	st->active = g_active;
	st->paused_manual = g_paused_manual;
	st->temp_cpu = g_temp_cpu;
	st->temp_gpu = g_temp_gpu;
	st->native_cpu = g_native_cpu;
	st->native_gpu = g_native_gpu;
	st->applied_cpu = g_last_cpu;
	st->applied_gpu = g_last_gpu;
	st->cfg = g_cfg;
}

int fa_runtime_set_enabled(int on)
{
	int err;
	struct ps_fan_aggressor_config loaded;

	/* Pick up engage/offset edits from conf, then apply requested enabled */
	ps_fan_aggressor_config_load(&loaded);
	g_cfg = loaded;
	g_cfg.enabled = on ? 1 : 0;
	err = ps_fan_aggressor_config_save(&g_cfg);
	if (err)
		return err;

	if (!on) {
		disengage();
		disarm_timer();
		g_paused_manual = 0;
		return 0;
	}
	return arm_timer(g_cfg.interval_ms);
}

int fa_runtime_set_config(const struct ps_fan_aggressor_config *cfg)
{
	int was = g_cfg.enabled;

	if (!cfg)
		return -EINVAL;
	g_cfg = *cfg;
	ps_fan_aggressor_config_save(&g_cfg);
	if (g_cfg.enabled) {
		if (!was || g_timer_fd < 0)
			return arm_timer(g_cfg.interval_ms);
		return arm_timer(g_cfg.interval_ms);
	}
	if (was)
		return fa_runtime_set_enabled(0);
	return 0;
}

void fa_runtime_on_timer(void)
{
	uint64_t exp;
	unsigned target_cpu, target_gpu, duty_c, duty_g, apply_c, apply_g;
	int cpu_c = 0, gpu_c = 0, tmax, disengage_c;
	time_t now;

	if (g_timer_fd < 0 || !g_cfg.enabled)
		return;
	if (read(g_timer_fd, &exp, sizeof(exp)) < 0)
		return;

	if (ps_available() != 0)
		return;

	if (ps_fan_target_get(&target_cpu, &target_gpu) == 0) {
		if (!(target_cpu == 0 && target_gpu == 0) &&
		    !(g_owned && target_cpu == g_last_cpu &&
		      target_gpu == g_last_gpu)) {
			g_paused_manual = 1;
			g_active = 0;
			g_owned = 0;
			g_hot_since = 0;
			return;
		}
	}
	g_paused_manual = 0;

	if (ps_temps_read(&cpu_c, &gpu_c) != 0)
		return;
	g_temp_cpu = cpu_c;
	g_temp_gpu = gpu_c;
	tmax = cpu_c > gpu_c ? cpu_c : gpu_c;
	disengage_c = g_cfg.engage_c - g_cfg.hysteresis_c;
	now = time(NULL);

	if (tmax < g_cfg.engage_c) {
		g_hot_since = 0;
		if (g_active && tmax <= disengage_c)
			disengage();
		else if (!g_active) {
			if (ps_fan_duty_get(&duty_c, &duty_g) == 0) {
				g_native_cpu = duty_c;
				g_native_gpu = duty_g;
			}
		}
		return;
	}

	if (g_hot_since == 0)
		g_hot_since = now;

	if (!g_active) {
		if ((now - g_hot_since) < g_cfg.hold_sec)
			return;
		if (ps_fan_duty_get(&duty_c, &duty_g) == 0) {
			g_native_cpu = duty_c;
			g_native_gpu = duty_g;
		}
		ps_fan_aggressor_compute_duty(&g_cfg, g_native_cpu, g_native_gpu,
					     &apply_c, &apply_g);
		if (ps_fan_set(apply_c, apply_g) == 0) {
			g_last_cpu = apply_c;
			g_last_gpu = apply_g;
			g_owned = 1;
			g_active = 1;
		}
		return;
	}

	ps_fan_aggressor_compute_duty(&g_cfg, g_native_cpu, g_native_gpu,
				     &apply_c, &apply_g);
	if (apply_c != g_last_cpu || apply_g != g_last_gpu) {
		if (ps_fan_set(apply_c, apply_g) == 0) {
			g_last_cpu = apply_c;
			g_last_gpu = apply_g;
			g_owned = 1;
		}
	}
}
