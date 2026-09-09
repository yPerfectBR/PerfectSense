/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * FanAggressor — offset mode helpers (config + duty math).
 * Timer lives in perfectsensed; this file is shared with the CLI.
 *
 * Load order: defaults → /etc/perfectsense/… → /run/perfectsense/… (override).
 * Save writes /run always and /etc when writable (persist across reboot).
 */

#include "perfectsense.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define PS_FA_ETC_PATH "/etc/perfectsense/fan-aggressor.conf"
#define PS_FA_RUN_PATH "/run/perfectsense/fan-aggressor.conf"

void ps_fan_aggressor_defaults(struct ps_fan_aggressor_config *cfg)
{
	if (!cfg)
		return;
	memset(cfg, 0, sizeof(*cfg));
	cfg->enabled = 0;
	cfg->engage_c = 90;
	cfg->hysteresis_c = 3;
	cfg->hold_sec = 15;
	cfg->interval_ms = 2000;
	cfg->offset_cpu = 20;
	cfg->offset_gpu = 20;
	cfg->floor_cpu = 55;
	cfg->floor_gpu = 55;
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

static void clamp_cfg(struct ps_fan_aggressor_config *cfg)
{
	if (cfg->engage_c < 50 || cfg->engage_c > 105)
		cfg->engage_c = 90;
	if (cfg->hysteresis_c < 0 || cfg->hysteresis_c > 20)
		cfg->hysteresis_c = 3;
	if (cfg->hold_sec < 0)
		cfg->hold_sec = 0;
	if (cfg->interval_ms < 500)
		cfg->interval_ms = 500;
	if (cfg->offset_cpu < 0)
		cfg->offset_cpu = 0;
	if (cfg->offset_gpu < 0)
		cfg->offset_gpu = 0;
	if (cfg->floor_cpu > 100)
		cfg->floor_cpu = 100;
	if (cfg->floor_gpu > 100)
		cfg->floor_gpu = 100;
}

static int load_file(const char *path, struct ps_fan_aggressor_config *cfg)
{
	FILE *f;
	char line[128];

	f = fopen(path, "r");
	if (!f)
		return (errno == ENOENT) ? 0 : -errno;

	while (fgets(line, sizeof(line), f)) {
		char *eq, *nl;

		nl = strchr(line, '\n');
		if (nl)
			*nl = '\0';
		if (line[0] == '#' || line[0] == '\0')
			continue;
		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq++ = '\0';
		if (!strcmp(line, "enabled"))
			cfg->enabled = atoi(eq) ? 1 : 0;
		else if (!strcmp(line, "engage_c"))
			cfg->engage_c = atoi(eq);
		else if (!strcmp(line, "hysteresis_c"))
			cfg->hysteresis_c = atoi(eq);
		else if (!strcmp(line, "hold_sec"))
			cfg->hold_sec = atoi(eq);
		else if (!strcmp(line, "interval_ms"))
			cfg->interval_ms = atoi(eq);
		else if (!strcmp(line, "offset_cpu"))
			cfg->offset_cpu = atoi(eq);
		else if (!strcmp(line, "offset_gpu"))
			cfg->offset_gpu = atoi(eq);
		else if (!strcmp(line, "floor_cpu"))
			cfg->floor_cpu = atoi(eq);
		else if (!strcmp(line, "floor_gpu"))
			cfg->floor_gpu = atoi(eq);
	}
	fclose(f);
	return 0;
}

int ps_fan_aggressor_config_load(struct ps_fan_aggressor_config *cfg)
{
	int err;

	if (!cfg)
		return -EINVAL;
	ps_fan_aggressor_defaults(cfg);
	err = load_file(PS_FA_ETC_PATH, cfg);
	if (err)
		return err;
	err = load_file(PS_FA_RUN_PATH, cfg);
	if (err)
		return err;
	clamp_cfg(cfg);
	return 0;
}

static int write_file(const char *path, const struct ps_fan_aggressor_config *cfg)
{
	FILE *f;

	f = fopen(path, "w");
	if (!f)
		return -errno;
	fprintf(f,
		"enabled=%d\n"
		"engage_c=%d\n"
		"hysteresis_c=%d\n"
		"hold_sec=%d\n"
		"interval_ms=%d\n"
		"offset_cpu=%d\n"
		"offset_gpu=%d\n"
		"floor_cpu=%d\n"
		"floor_gpu=%d\n",
		cfg->enabled ? 1 : 0, cfg->engage_c, cfg->hysteresis_c,
		cfg->hold_sec, cfg->interval_ms, cfg->offset_cpu,
		cfg->offset_gpu, cfg->floor_cpu, cfg->floor_gpu);
	if (fclose(f) != 0)
		return -errno;
	return 0;
}

int ps_fan_aggressor_config_save(const struct ps_fan_aggressor_config *cfg)
{
	int err;

	if (!cfg)
		return -EINVAL;
	err = ensure_dir("/run/perfectsense", 0755);
	if (err)
		return err;
	err = write_file(PS_FA_RUN_PATH, cfg);
	if (err)
		return err;

	/* Best-effort persist for reboot (ignore permission errors). */
	if (ensure_dir("/etc/perfectsense", 0755) == 0)
		(void)write_file(PS_FA_ETC_PATH, cfg);
	return 0;
}

void ps_fan_aggressor_compute_duty(const struct ps_fan_aggressor_config *cfg,
				   unsigned native_cpu, unsigned native_gpu,
				   unsigned *out_cpu, unsigned *out_gpu)
{
	unsigned c, g;

	if (!cfg || !out_cpu || !out_gpu)
		return;
	c = native_cpu + (unsigned)cfg->offset_cpu;
	g = native_gpu + (unsigned)cfg->offset_gpu;
	if (c < (unsigned)cfg->floor_cpu)
		c = (unsigned)cfg->floor_cpu;
	if (g < (unsigned)cfg->floor_gpu)
		g = (unsigned)cfg->floor_gpu;
	if (c > 100)
		c = 100;
	if (g > 100)
		g = 100;
	*out_cpu = c;
	*out_gpu = g;
}
