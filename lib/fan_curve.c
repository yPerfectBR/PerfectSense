/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * PerfectSense — custom fan curve (per-axis temp bands → duty %).
 *
 * Band: (temp_lo, temp_hi, duty%) e.g. (90,100,95) → while temp in 90–100 °C,
 * that fan runs at 95%. CPU and GPU axes are independent.
 *
 * Timer / apply lives in perfectsensed.
 */

#define _POSIX_C_SOURCE 200809L

#include "perfectsense.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define PS_FC_ETC_PATH "/etc/perfectsense/fan-curve.conf"
#define PS_FC_RUN_PATH "/run/perfectsense/fan-curve.conf"

static void axis_defaults(struct ps_fan_curve_axis *ax, int cpu_like)
{
	memset(ax, 0, sizeof(*ax));
	ax->n_bands = 4;
	if (cpu_like) {
		ax->bands[0] = (struct ps_fan_curve_band){ 40, 59, 30 };
		ax->bands[1] = (struct ps_fan_curve_band){ 60, 74, 45 };
		ax->bands[2] = (struct ps_fan_curve_band){ 75, 89, 70 };
		ax->bands[3] = (struct ps_fan_curve_band){ 90, 105, 100 };
	} else {
		ax->bands[0] = (struct ps_fan_curve_band){ 40, 59, 25 };
		ax->bands[1] = (struct ps_fan_curve_band){ 60, 74, 40 };
		ax->bands[2] = (struct ps_fan_curve_band){ 75, 89, 65 };
		ax->bands[3] = (struct ps_fan_curve_band){ 90, 105, 95 };
	}
}

void ps_fan_curve_defaults(struct ps_fan_curve_config *cfg)
{
	if (!cfg)
		return;
	memset(cfg, 0, sizeof(*cfg));
	cfg->enabled = 0;
	cfg->interval_ms = 2000;
	axis_defaults(&cfg->cpu, 1);
	axis_defaults(&cfg->gpu, 0);
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

static int cmp_bands(const void *a, const void *b)
{
	const struct ps_fan_curve_band *ba = a;
	const struct ps_fan_curve_band *bb = b;

	if (ba->temp_lo != bb->temp_lo)
		return ba->temp_lo - bb->temp_lo;
	return ba->temp_hi - bb->temp_hi;
}

static void clamp_axis(struct ps_fan_curve_axis *ax)
{
	int i;

	if (ax->n_bands < 1)
		ax->n_bands = 1;
	if (ax->n_bands > PS_FAN_CURVE_MAX_BANDS)
		ax->n_bands = PS_FAN_CURVE_MAX_BANDS;
	for (i = 0; i < ax->n_bands; i++) {
		struct ps_fan_curve_band *b = &ax->bands[i];

		if (b->temp_lo < 0)
			b->temp_lo = 0;
		if (b->temp_hi > 120)
			b->temp_hi = 120;
		if (b->temp_hi < b->temp_lo) {
			int t = b->temp_lo;

			b->temp_lo = b->temp_hi;
			b->temp_hi = t;
		}
		if (b->duty_pct > 100)
			b->duty_pct = 100;
	}
	qsort(ax->bands, (size_t)ax->n_bands, sizeof(ax->bands[0]), cmp_bands);
}

static void clamp_cfg(struct ps_fan_curve_config *cfg)
{
	if (cfg->interval_ms < 500)
		cfg->interval_ms = 500;
	clamp_axis(&cfg->cpu);
	clamp_axis(&cfg->gpu);
}

static int parse_one_band(const char *s, struct ps_fan_curve_band *out)
{
	int lo, hi;
	unsigned duty;
	const char *p = s;

	while (*p && isspace((unsigned char)*p))
		p++;
	if (*p == '(')
		p++;
	if (sscanf(p, "%d %*[,:] %d %*[,:] %u", &lo, &hi, &duty) != 3 &&
	    sscanf(p, "%d,%d,%u", &lo, &hi, &duty) != 3 &&
	    sscanf(p, "%d:%d:%u", &lo, &hi, &duty) != 3)
		return -EINVAL;
	out->temp_lo = lo;
	out->temp_hi = hi;
	out->duty_pct = duty;
	return 0;
}

int ps_fan_curve_parse_bands(const char *text, struct ps_fan_curve_axis *axis)
{
	char buf[512];
	char *p;
	int n = 0;

	if (!text || !axis)
		return -EINVAL;
	snprintf(buf, sizeof(buf), "%s", text);
	p = buf;
	while (*p && n < PS_FAN_CURVE_MAX_BANDS) {
		struct ps_fan_curve_band b;
		char *start;
		char *end;
		char saved;
		int lo, hi, eaten = 0;
		unsigned d;

		while (*p && (isspace((unsigned char)*p) || *p == ',' ||
			      *p == ')'))
			p++;
		if (!*p)
			break;
		start = p;
		if (*start == '(') {
			end = strchr(start, ')');
			if (!end)
				return -EINVAL;
			end++;
		} else if (sscanf(start, "%d%*[,:]%d%*[,:]%u%n", &lo, &hi, &d,
				  &eaten) == 3) {
			end = start + eaten;
		} else {
			return -EINVAL;
		}
		saved = *end;
		*end = '\0';
		if (parse_one_band(start, &b) != 0) {
			*end = saved;
			return -EINVAL;
		}
		*end = saved;
		axis->bands[n++] = b;
		p = end;
	}
	if (n < 1)
		return -EINVAL;
	axis->n_bands = n;
	clamp_axis(axis);
	return 0;
}

static int load_file(const char *path, struct ps_fan_curve_config *cfg)
{
	FILE *f;
	char line[256];
	int n_cpu = 0, n_gpu = 0;
	struct ps_fan_curve_band cpu_tmp[PS_FAN_CURVE_MAX_BANDS];
	struct ps_fan_curve_band gpu_tmp[PS_FAN_CURVE_MAX_BANDS];

	f = fopen(path, "r");
	if (!f)
		return (errno == ENOENT) ? 0 : -errno;

	while (fgets(line, sizeof(line), f)) {
		char *eq, *nl;
		struct ps_fan_curve_band b;

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
		else if (!strcmp(line, "cpu_band") &&
			 n_cpu < PS_FAN_CURVE_MAX_BANDS) {
			if (parse_one_band(eq, &b) == 0)
				cpu_tmp[n_cpu++] = b;
		} else if (!strcmp(line, "gpu_band") &&
			   n_gpu < PS_FAN_CURVE_MAX_BANDS) {
			if (parse_one_band(eq, &b) == 0)
				gpu_tmp[n_gpu++] = b;
		} else if (!strcmp(line, "cpu") || !strcmp(line, "gpu")) {
			struct ps_fan_curve_axis ax;
			int err = ps_fan_curve_parse_bands(eq, &ax);

			if (err == 0) {
				if (!strcmp(line, "cpu"))
					cfg->cpu = ax;
				else
					cfg->gpu = ax;
			}
		}
	}
	fclose(f);
	if (n_cpu >= 1) {
		memcpy(cfg->cpu.bands, cpu_tmp, sizeof(*cpu_tmp) * (size_t)n_cpu);
		cfg->cpu.n_bands = n_cpu;
	}
	if (n_gpu >= 1) {
		memcpy(cfg->gpu.bands, gpu_tmp, sizeof(*gpu_tmp) * (size_t)n_gpu);
		cfg->gpu.n_bands = n_gpu;
	}
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
	for (i = 0; i < cfg->cpu.n_bands; i++)
		fprintf(f, "cpu_band=%d:%d:%u\n", cfg->cpu.bands[i].temp_lo,
			cfg->cpu.bands[i].temp_hi, cfg->cpu.bands[i].duty_pct);
	for (i = 0; i < cfg->gpu.n_bands; i++)
		fprintf(f, "gpu_band=%d:%d:%u\n", cfg->gpu.bands[i].temp_lo,
			cfg->gpu.bands[i].temp_hi, cfg->gpu.bands[i].duty_pct);
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

static unsigned lerp_u(unsigned a, unsigned b, int t0, int t1, int t)
{
	int span = t1 - t0;
	long v;

	if (span <= 0)
		return b;
	v = (long)a + ((long)b - (long)a) * (t - t0) / span;
	if (v < 0)
		v = 0;
	if (v > 100)
		v = 100;
	return (unsigned)v;
}

static unsigned eval_axis(const struct ps_fan_curve_axis *ax, int temp_c)
{
	int i;
	const struct ps_fan_curve_band *prev = NULL;

	if (!ax || ax->n_bands < 1)
		return 0;

	if (temp_c < ax->bands[0].temp_lo)
		return 0;

	for (i = 0; i < ax->n_bands; i++) {
		const struct ps_fan_curve_band *b = &ax->bands[i];

		if (temp_c >= b->temp_lo && temp_c <= b->temp_hi)
			return b->duty_pct;
		if (prev && temp_c > prev->temp_hi && temp_c < b->temp_lo)
			return lerp_u(prev->duty_pct, b->duty_pct, prev->temp_hi,
				      b->temp_lo, temp_c);
		prev = b;
	}
	/* above all bands */
	return ax->bands[ax->n_bands - 1].duty_pct;
}

void ps_fan_curve_eval(const struct ps_fan_curve_config *cfg, int cpu_c,
		       int gpu_c, unsigned *out_cpu, unsigned *out_gpu)
{
	if (!cfg || !out_cpu || !out_gpu)
		return;
	*out_cpu = eval_axis(&cfg->cpu, cpu_c);
	*out_gpu = eval_axis(&cfg->gpu, gpu_c);
}
