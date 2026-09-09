/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * PerfectSense — custom fan curve (temp → duty points).
 * Timer / apply lives in perfectsensed; this file is shared with the CLI.
 *
 * Load: defaults → /etc/perfectsense/fan-curve.conf → /run/… (override).
 * Save: /run always; /etc when writable.
 */

#define _POSIX_C_SOURCE 200809L

#include "perfectsense.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define PS_FC_ETC_PATH "/etc/perfectsense/fan-curve.conf"
#define PS_FC_RUN_PATH "/run/perfectsense/fan-curve.conf"

void ps_fan_curve_defaults(struct ps_fan_curve_config *cfg)
{
	if (!cfg)
		return;
	memset(cfg, 0, sizeof(*cfg));
	cfg->enabled = 0;
	cfg->interval_ms = 2000;
	cfg->n_points = 5;
	cfg->points[0] = (struct ps_fan_curve_point){ 40, 25, 20 };
	cfg->points[1] = (struct ps_fan_curve_point){ 60, 35, 30 };
	cfg->points[2] = (struct ps_fan_curve_point){ 75, 50, 45 };
	cfg->points[3] = (struct ps_fan_curve_point){ 85, 70, 65 };
	cfg->points[4] = (struct ps_fan_curve_point){ 95, 100, 90 };
}

static int ensure_dir(const char *path, mode_t mode)
{
	struct stat st;

	if (stat(path, &st) == 0)
		return S_ISDIR(st.st_mode) ? 0 : -ENOTDIR;
	if (mkdir(path, mode) != 0 && errno != EEXIST)
		return -errno;
	return 0;
}

static int cmp_points(const void *a, const void *b)
{
	const struct ps_fan_curve_point *pa = a;
	const struct ps_fan_curve_point *pb = b;

	return pa->temp_c - pb->temp_c;
}

static void clamp_cfg(struct ps_fan_curve_config *cfg)
{
	int i;

	if (cfg->interval_ms < 500)
		cfg->interval_ms = 500;
	if (cfg->n_points < 2)
		cfg->n_points = 2;
	if (cfg->n_points > PS_FAN_CURVE_MAX_POINTS)
		cfg->n_points = PS_FAN_CURVE_MAX_POINTS;
	for (i = 0; i < cfg->n_points; i++) {
		if (cfg->points[i].temp_c < 0)
			cfg->points[i].temp_c = 0;
		if (cfg->points[i].temp_c > 110)
			cfg->points[i].temp_c = 110;
		if (cfg->points[i].cpu_pct > 100)
			cfg->points[i].cpu_pct = 100;
		if (cfg->points[i].gpu_pct > 100)
			cfg->points[i].gpu_pct = 100;
	}
	qsort(cfg->points, (size_t)cfg->n_points, sizeof(cfg->points[0]),
	      cmp_points);
}

static int load_file(const char *path, struct ps_fan_curve_config *cfg)
{
	FILE *f;
	char line[192];
	int n = 0;

	f = fopen(path, "r");
	if (!f)
		return (errno == ENOENT) ? 0 : -errno;

	while (fgets(line, sizeof(line), f)) {
		char *eq, *nl;
		int t;
		unsigned c, g;

		nl = strchr(line, '\n');
		if (nl)
			*nl = '\0';
		if (line[0] == '#' || line[0] == '\0')
			continue;
		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		eq++;
		if (!strcmp(line, "enabled"))
			cfg->enabled = atoi(eq) ? 1 : 0;
		else if (!strcmp(line, "interval_ms"))
			cfg->interval_ms = atoi(eq);
		else if (!strcmp(line, "point") &&
			 n < PS_FAN_CURVE_MAX_POINTS) {
			if (sscanf(eq, "%d:%u:%u", &t, &c, &g) == 3) {
				cfg->points[n].temp_c = t;
				cfg->points[n].cpu_pct = c;
				cfg->points[n].gpu_pct = g;
				n++;
			}
		}
	}
	fclose(f);
	if (n >= 2)
		cfg->n_points = n;
	return 0;
}

int ps_fan_curve_config_load(struct ps_fan_curve_config *cfg)
{
	int err;

	if (!cfg)
		return -EINVAL;
	ps_fan_curve_defaults(cfg);
	err = load_file(PS_FC_ETC_PATH, cfg);
	if (err)
		return err;
	err = load_file(PS_FC_RUN_PATH, cfg);
	if (err)
		return err;
	clamp_cfg(cfg);
	return 0;
}

static int write_file(const char *path, const struct ps_fan_curve_config *cfg)
{
	FILE *f;
	int i;

	f = fopen(path, "w");
	if (!f)
		return -errno;
	fprintf(f, "enabled=%d\ninterval_ms=%d\n", cfg->enabled ? 1 : 0,
		cfg->interval_ms);
	for (i = 0; i < cfg->n_points; i++)
		fprintf(f, "point=%d:%u:%u\n", cfg->points[i].temp_c,
			cfg->points[i].cpu_pct, cfg->points[i].gpu_pct);
	if (fclose(f) != 0)
		return -errno;
	return 0;
}

int ps_fan_curve_config_save(const struct ps_fan_curve_config *cfg)
{
	struct ps_fan_curve_config copy;
	int err;

	if (!cfg)
		return -EINVAL;
	copy = *cfg;
	clamp_cfg(&copy);

	err = ensure_dir("/run/perfectsense", 0755);
	if (err)
		return err;
	err = write_file(PS_FC_RUN_PATH, &copy);
	if (err)
		return err;

	if (ensure_dir("/etc/perfectsense", 0755) == 0)
		(void)write_file(PS_FC_ETC_PATH, &copy);
	return 0;
}

int ps_fan_curve_parse_points(const char *csv, struct ps_fan_curve_config *cfg)
{
	char buf[512];
	char *save = NULL;
	char *tok;
	int n = 0;

	if (!csv || !cfg)
		return -EINVAL;
	snprintf(buf, sizeof(buf), "%s", csv);
	for (tok = strtok_r(buf, ",", &save); tok;
	     tok = strtok_r(NULL, ",", &save)) {
		int t;
		unsigned c, g;

		if (n >= PS_FAN_CURVE_MAX_POINTS)
			return -EINVAL;
		if (sscanf(tok, "%d:%u:%u", &t, &c, &g) != 3)
			return -EINVAL;
		cfg->points[n].temp_c = t;
		cfg->points[n].cpu_pct = c;
		cfg->points[n].gpu_pct = g;
		n++;
	}
	if (n < 2)
		return -EINVAL;
	cfg->n_points = n;
	clamp_cfg(cfg);
	return 0;
}

static unsigned lerp_u(unsigned a, unsigned b, int t0, int t1, int t)
{
	int span, off;
	long v;

	if (t1 <= t0)
		return b;
	span = t1 - t0;
	off = t - t0;
	v = (long)a + ((long)b - (long)a) * off / span;
	if (v < 0)
		v = 0;
	if (v > 100)
		v = 100;
	return (unsigned)v;
}

static unsigned eval_axis(const struct ps_fan_curve_config *cfg, int temp_c,
			  int cpu_axis)
{
	int i;
	const struct ps_fan_curve_point *p0, *p1;

	if (!cfg || cfg->n_points < 1)
		return 0;
	if (temp_c <= cfg->points[0].temp_c)
		return cpu_axis ? cfg->points[0].cpu_pct
				: cfg->points[0].gpu_pct;
	if (temp_c >= cfg->points[cfg->n_points - 1].temp_c)
		return cpu_axis ? cfg->points[cfg->n_points - 1].cpu_pct
				: cfg->points[cfg->n_points - 1].gpu_pct;

	for (i = 0; i < cfg->n_points - 1; i++) {
		p0 = &cfg->points[i];
		p1 = &cfg->points[i + 1];
		if (temp_c >= p0->temp_c && temp_c <= p1->temp_c) {
			unsigned a = cpu_axis ? p0->cpu_pct : p0->gpu_pct;
			unsigned b = cpu_axis ? p1->cpu_pct : p1->gpu_pct;

			return lerp_u(a, b, p0->temp_c, p1->temp_c, temp_c);
		}
	}
	return cpu_axis ? cfg->points[cfg->n_points - 1].cpu_pct
			: cfg->points[cfg->n_points - 1].gpu_pct;
}

void ps_fan_curve_eval(const struct ps_fan_curve_config *cfg, int cpu_c,
		       int gpu_c, unsigned *out_cpu, unsigned *out_gpu)
{
	if (!cfg || !out_cpu || !out_gpu)
		return;
	*out_cpu = eval_axis(cfg, cpu_c, 1);
	*out_gpu = eval_axis(cfg, gpu_c, 0);
}
