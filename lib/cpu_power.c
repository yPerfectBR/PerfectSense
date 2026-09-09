/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * PerfectSense — CPU power helpers (cpufreq / intel_pstate / RAPL).
 * Pure userspace sysfs; no WMI. Event/command driven only.
 */

#include "perfectsense.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PS_CPU0_BASE "/sys/devices/system/cpu/cpu0/cpufreq"
#define PS_PSTATE_BASE "/sys/devices/system/cpu/intel_pstate"
#define PS_RAPL_PKG "/sys/class/powercap/intel-rapl:0"
#define PS_CPU_SNAP_PATH "/run/perfectsense/cpu-power.snap"

static int read_trimmed(const char *path, char *buf, size_t buflen)
{
	FILE *f;
	size_t n;
	char *p;

	if (!path || !buf || buflen == 0)
		return -EINVAL;
	f = fopen(path, "r");
	if (!f)
		return -errno;
	if (!fgets(buf, (int)buflen, f)) {
		int err = ferror(f) ? -errno : -EIO;
		fclose(f);
		return err;
	}
	fclose(f);
	n = strlen(buf);
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
			 buf[n - 1] == ' ' || buf[n - 1] == '\t'))
		buf[--n] = '\0';
	p = buf;
	while (*p == ' ' || *p == '\t')
		p++;
	if (p != buf)
		memmove(buf, p, strlen(p) + 1);
	return 0;
}

static int write_text(const char *path, const char *text)
{
	FILE *f;
	int n;

	if (!path || !text)
		return -EINVAL;
	f = fopen(path, "w");
	if (!f)
		return -errno;
	n = fprintf(f, "%s\n", text);
	if (n < 0) {
		int err = -errno;
		fclose(f);
		return err;
	}
	if (fclose(f) != 0)
		return -errno;
	return 0;
}

static int read_ulong(const char *path, unsigned long *out)
{
	char buf[64];
	char *end;
	unsigned long v;
	int err;

	err = read_trimmed(path, buf, sizeof(buf));
	if (err)
		return err;
	v = strtoul(buf, &end, 10);
	if (end == buf || *end != '\0')
		return -EINVAL;
	*out = v;
	return 0;
}

static int write_ulong(const char *path, unsigned long v)
{
	char buf[32];

	snprintf(buf, sizeof(buf), "%lu", v);
	return write_text(path, buf);
}

static int for_each_cpu_cpufreq(int (*fn)(const char *cpu_cpufreq_dir, void *ctx),
				void *ctx)
{
	DIR *d;
	struct dirent *de;
	int last_err = 0;

	d = opendir("/sys/devices/system/cpu");
	if (!d)
		return -errno;

	while ((de = readdir(d)) != NULL) {
		char path[320];
		struct stat st;

		if (strncmp(de->d_name, "cpu", 3) != 0)
			continue;
		if (de->d_name[3] < '0' || de->d_name[3] > '9')
			continue;
		snprintf(path, sizeof(path),
			 "/sys/devices/system/cpu/%s/cpufreq", de->d_name);
		if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
			continue;
		{
			int err = fn(path, ctx);
			if (err)
				last_err = err;
		}
	}
	closedir(d);
	return last_err;
}

struct write_str_ctx {
	const char *leaf;
	const char *value;
};

static int write_leaf(const char *cpu_cpufreq_dir, void *ctx)
{
	struct write_str_ctx *c = ctx;
	char path[300];

	snprintf(path, sizeof(path), "%s/%s", cpu_cpufreq_dir, c->leaf);
	return write_text(path, c->value);
}

int ps_cpu_power_available(void)
{
	return access(PS_CPU0_BASE "/scaling_governor", R_OK) == 0 ? 0 : -ENODEV;
}

int ps_cpu_power_read(struct ps_cpu_power *out)
{
	char path[256];
	unsigned long v;
	int err;

	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));

	err = ps_cpu_power_available();
	if (err)
		return err;
	out->available = 1;

	err = read_trimmed(PS_CPU0_BASE "/scaling_governor", out->governor,
			   sizeof(out->governor));
	if (err)
		return err;

	if (read_trimmed(PS_CPU0_BASE "/energy_performance_preference",
			 out->epp, sizeof(out->epp)) != 0)
		out->epp[0] = '\0';

	if (read_ulong(PS_CPU0_BASE "/scaling_max_freq", &v) == 0)
		out->scaling_max_khz = (unsigned)v;
	if (read_ulong(PS_CPU0_BASE "/scaling_min_freq", &v) == 0)
		out->scaling_min_khz = (unsigned)v;
	if (read_ulong(PS_CPU0_BASE "/cpuinfo_max_freq", &v) == 0)
		out->cpuinfo_max_khz = (unsigned)v;

	out->no_turbo = -1;
	if (read_ulong(PS_PSTATE_BASE "/no_turbo", &v) == 0)
		out->no_turbo = v ? 1 : 0;
	if (read_ulong(PS_PSTATE_BASE "/max_perf_pct", &v) == 0)
		out->max_perf_pct = (unsigned)v;
	if (read_ulong(PS_PSTATE_BASE "/min_perf_pct", &v) == 0)
		out->min_perf_pct = (unsigned)v;

	out->pl1_uw = 0;
	out->pl2_uw = 0;
	snprintf(path, sizeof(path), "%s/constraint_0_power_limit_uw", PS_RAPL_PKG);
	if (read_ulong(path, &v) == 0)
		out->pl1_uw = v;
	snprintf(path, sizeof(path), "%s/constraint_1_power_limit_uw", PS_RAPL_PKG);
	if (read_ulong(path, &v) == 0)
		out->pl2_uw = v;

	return 0;
}

int ps_cpu_power_set_governor(const char *name)
{
	struct write_str_ctx ctx = { .leaf = "scaling_governor", .value = name };

	if (!name || !name[0])
		return -EINVAL;
	return for_each_cpu_cpufreq(write_leaf, &ctx);
}

int ps_cpu_power_set_epp(const char *name)
{
	struct write_str_ctx ctx = {
		.leaf = "energy_performance_preference",
		.value = name,
	};

	if (!name || !name[0])
		return -EINVAL;
	return for_each_cpu_cpufreq(write_leaf, &ctx);
}

int ps_cpu_power_set_turbo(int enabled)
{
	/* enabled=1 → allow turbo → no_turbo=0 */
	return write_ulong(PS_PSTATE_BASE "/no_turbo", enabled ? 0UL : 1UL);
}

int ps_cpu_power_set_max_perf_pct(unsigned pct)
{
	if (pct < 1 || pct > 100)
		return -EINVAL;
	return write_ulong(PS_PSTATE_BASE "/max_perf_pct", pct);
}

int ps_cpu_power_set_scaling_max_khz(unsigned khz)
{
	struct write_str_ctx ctx;
	char buf[32];

	if (khz == 0)
		return -EINVAL;
	snprintf(buf, sizeof(buf), "%u", khz);
	ctx.leaf = "scaling_max_freq";
	ctx.value = buf;
	return for_each_cpu_cpufreq(write_leaf, &ctx);
}

int ps_cpu_power_set_pl1_uw(unsigned long uw)
{
	char path[256];

	snprintf(path, sizeof(path), "%s/constraint_0_power_limit_uw", PS_RAPL_PKG);
	return write_ulong(path, uw);
}

int ps_cpu_power_set_pl2_uw(unsigned long uw)
{
	char path[256];

	snprintf(path, sizeof(path), "%s/constraint_1_power_limit_uw", PS_RAPL_PKG);
	return write_ulong(path, uw);
}

static int ensure_run_dir(void)
{
	struct stat st;

	if (stat("/run/perfectsense", &st) == 0)
		return S_ISDIR(st.st_mode) ? 0 : -ENOTDIR;
	if (mkdir("/run/perfectsense", 0755) != 0 && errno != EEXIST)
		return -errno;
	return 0;
}

int ps_cpu_power_snapshot_save(void)
{
	struct ps_cpu_power cur;
	FILE *f;
	int err;

	err = ps_cpu_power_read(&cur);
	if (err)
		return err;
	err = ensure_run_dir();
	if (err)
		return err;

	f = fopen(PS_CPU_SNAP_PATH, "w");
	if (!f)
		return -errno;
	fprintf(f,
		"governor=%s\n"
		"epp=%s\n"
		"no_turbo=%d\n"
		"max_perf_pct=%u\n"
		"min_perf_pct=%u\n"
		"scaling_max_khz=%u\n"
		"scaling_min_khz=%u\n"
		"pl1_uw=%lu\n"
		"pl2_uw=%lu\n",
		cur.governor, cur.epp, cur.no_turbo, cur.max_perf_pct,
		cur.min_perf_pct, cur.scaling_max_khz, cur.scaling_min_khz,
		cur.pl1_uw, cur.pl2_uw);
	if (fclose(f) != 0)
		return -errno;
	return 0;
}

int ps_cpu_power_snapshot_clear(void)
{
	if (unlink(PS_CPU_SNAP_PATH) != 0 && errno != ENOENT)
		return -errno;
	return 0;
}

int ps_cpu_power_restore(void)
{
	FILE *f;
	char line[128];
	char governor[32] = "";
	char epp[32] = "";
	int no_turbo = -1;
	unsigned max_perf = 0, scaling_max = 0;
	unsigned long pl1 = 0, pl2 = 0;
	int err = 0;

	f = fopen(PS_CPU_SNAP_PATH, "r");
	if (!f)
		return -errno;

	while (fgets(line, sizeof(line), f)) {
		char *nl = strchr(line, '\n');
		char *eq;

		if (nl)
			*nl = '\0';
		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';
		eq++;
		if (!strcmp(line, "governor"))
			snprintf(governor, sizeof(governor), "%s", eq);
		else if (!strcmp(line, "epp"))
			snprintf(epp, sizeof(epp), "%s", eq);
		else if (!strcmp(line, "no_turbo"))
			no_turbo = atoi(eq);
		else if (!strcmp(line, "max_perf_pct"))
			max_perf = (unsigned)atoi(eq);
		else if (!strcmp(line, "scaling_max_khz"))
			scaling_max = (unsigned)strtoul(eq, NULL, 10);
		else if (!strcmp(line, "pl1_uw"))
			pl1 = strtoul(eq, NULL, 10);
		else if (!strcmp(line, "pl2_uw"))
			pl2 = strtoul(eq, NULL, 10);
	}
	fclose(f);

	if (governor[0]) {
		err = ps_cpu_power_set_governor(governor);
		if (err)
			return err;
	}
	if (epp[0]) {
		err = ps_cpu_power_set_epp(epp);
		if (err)
			return err;
	}
	if (no_turbo == 0 || no_turbo == 1) {
		err = ps_cpu_power_set_turbo(no_turbo ? 0 : 1);
		if (err)
			return err;
	}
	if (max_perf >= 1 && max_perf <= 100) {
		err = ps_cpu_power_set_max_perf_pct(max_perf);
		if (err)
			return err;
	}
	if (scaling_max > 0) {
		err = ps_cpu_power_set_scaling_max_khz(scaling_max);
		if (err)
			return err;
	}
	if (pl1 > 0) {
		err = ps_cpu_power_set_pl1_uw(pl1);
		if (err)
			return err;
	}
	if (pl2 > 0) {
		err = ps_cpu_power_set_pl2_uw(pl2);
		if (err)
			return err;
	}
	return 0;
}

int ps_cpu_power_preset(const char *name, int sync_platform_profile)
{
	const char *gov = "powersave";
	const char *epp = "balance_performance";
	const char *platform = NULL;
	int turbo = 1;
	unsigned max_perf = 100;
	int err;

	if (!name)
		return -EINVAL;

	if (!strcmp(name, "power") || !strcmp(name, "eco")) {
		epp = "power";
		platform = "low-power";
	} else if (!strcmp(name, "balanced")) {
		epp = "balance_performance";
		platform = "balanced";
	} else if (!strcmp(name, "performance") || !strcmp(name, "bp")) {
		gov = "performance";
		epp = "performance";
		platform = "balanced-performance";
	} else if (!strcmp(name, "turbo")) {
		gov = "performance";
		epp = "performance";
		platform = "performance";
	} else {
		return -EINVAL;
	}

	err = ps_cpu_power_available();
	if (err)
		return err;

	/* Keep first snapshot as restore baseline until explicit resnap */
	if (access(PS_CPU_SNAP_PATH, F_OK) != 0) {
		err = ps_cpu_power_snapshot_save();
		if (err)
			return err;
	}

	/*
	 * Apply thermal profile first when syncing: AC/firmware may reject
	 * aggressive modes. Fail before mutating CPU knobs.
	 * ps_mode_set also applies CPU for the profile; we re-apply below so
	 * named presets stay authoritative for gov/epp.
	 */
	if (sync_platform_profile && platform && ps_available() == 0) {
		err = ps_mode_set(platform);
		if (err)
			return err;
	}

	err = ps_cpu_power_set_governor(gov);
	if (err)
		return err;
	err = ps_cpu_power_set_epp(epp);
	if (err)
		return err;
	err = ps_cpu_power_set_turbo(turbo);
	if (err)
		return err;
	err = ps_cpu_power_set_max_perf_pct(max_perf);
	if (err)
		return err;

	return 0;
}

/*
 * Map firmware thermal_profile → cpufreq governor / EPP.
 * Quiet uses balance_power; performance (Predator "turbo") locks
 * scaling_governor=performance. Best-effort: missing CPU sysfs → -ENODEV.
 */
int ps_cpu_power_for_profile(const char *thermal_name)
{
	const char *gov = "powersave";
	const char *epp = "balance_performance";
	int err;

	if (!thermal_name || !thermal_name[0])
		return -EINVAL;

	if (!strcmp(thermal_name, "low-power")) {
		epp = "power";
	} else if (!strcmp(thermal_name, "quiet")) {
		epp = "balance_power";
	} else if (!strcmp(thermal_name, "balanced")) {
		epp = "balance_performance";
	} else if (!strcmp(thermal_name, "balanced-performance") ||
		   !strcmp(thermal_name, "performance")) {
		/* From "desempenho" up: lock governor to performance */
		gov = "performance";
		epp = "performance";
	} else {
		return -EINVAL;
	}

	err = ps_cpu_power_available();
	if (err)
		return err;

	if (access(PS_CPU_SNAP_PATH, F_OK) != 0) {
		err = ps_cpu_power_snapshot_save();
		if (err)
			return err;
	}

	err = ps_cpu_power_set_governor(gov);
	if (err)
		return err;
	err = ps_cpu_power_set_epp(epp);
	if (err)
		return err;
	err = ps_cpu_power_set_turbo(1);
	if (err)
		return err;
	return ps_cpu_power_set_max_perf_pct(100);
}
