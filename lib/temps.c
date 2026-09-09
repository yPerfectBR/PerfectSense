/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Temperature + fan RPM helpers (perfectsense/acer hwmon + coretemp).
 */

#include "perfectsense.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_long(const char *path, long *out)
{
	FILE *f;
	long v;

	f = fopen(path, "r");
	if (!f)
		return -errno;
	if (fscanf(f, "%ld", &v) != 1) {
		fclose(f);
		return -EIO;
	}
	fclose(f);
	*out = v;
	return 0;
}

static int read_millideg_c(const char *path, int *out_c)
{
	long v;
	int err = read_long(path, &v);

	if (err)
		return err;
	*out_c = (int)(v / 1000);
	return 0;
}

static int find_hwmon(const char *name, char *out, size_t outlen)
{
	DIR *d;
	struct dirent *de;

	d = opendir("/sys/class/hwmon");
	if (!d)
		return -errno;

	while ((de = readdir(d)) != NULL) {
		char path[256];
		char nbuf[64];
		FILE *f;

		if (strncmp(de->d_name, "hwmon", 5) != 0)
			continue;
		snprintf(path, sizeof(path), "/sys/class/hwmon/%s/name",
			 de->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		if (!fgets(nbuf, sizeof(nbuf), f)) {
			fclose(f);
			continue;
		}
		fclose(f);
		nbuf[strcspn(nbuf, "\n")] = '\0';
		if (strcmp(nbuf, name) != 0)
			continue;
		snprintf(out, outlen, "/sys/class/hwmon/%s", de->d_name);
		closedir(d);
		return 0;
	}
	closedir(d);
	return -ENOENT;
}

static int read_named_hwmon_pair(const char *name, int *cpu_c, int *gpu_c,
				 int *cpu_rpm, int *gpu_rpm)
{
	char base[128];
	char path[192];
	int t;
	long rpm;

	if (find_hwmon(name, base, sizeof(base)) != 0)
		return -ENOENT;

	if (cpu_c) {
		snprintf(path, sizeof(path), "%s/temp1_input", base);
		if (read_millideg_c(path, &t) == 0)
			*cpu_c = t;
	}
	if (gpu_c) {
		snprintf(path, sizeof(path), "%s/temp2_input", base);
		if (read_millideg_c(path, &t) == 0)
			*gpu_c = t;
	}
	if (cpu_rpm) {
		snprintf(path, sizeof(path), "%s/fan1_input", base);
		if (read_long(path, &rpm) == 0)
			*cpu_rpm = (int)rpm;
	}
	if (gpu_rpm) {
		snprintf(path, sizeof(path), "%s/fan2_input", base);
		if (read_long(path, &rpm) == 0)
			*gpu_rpm = (int)rpm;
	}
	return 0;
}

static void raise_cpu_coretemp(int *tcpu)
{
	DIR *d;
	struct dirent *de;
	char path[192];
	int t;

	d = opendir("/sys/class/hwmon");
	if (!d)
		return;

	while ((de = readdir(d)) != NULL) {
		char label[64];
		FILE *f;

		if (strncmp(de->d_name, "hwmon", 5) != 0)
			continue;
		snprintf(path, sizeof(path),
			 "/sys/class/hwmon/%s/temp1_label", de->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		if (!fgets(label, sizeof(label), f)) {
			fclose(f);
			continue;
		}
		fclose(f);
		label[strcspn(label, "\n")] = '\0';
		if (strcmp(label, "Package id 0") != 0)
			continue;
		snprintf(path, sizeof(path),
			 "/sys/class/hwmon/%s/temp1_input", de->d_name);
		if (read_millideg_c(path, &t) == 0 && t > *tcpu)
			*tcpu = t;
		break;
	}
	closedir(d);
}

static void try_gpu_vendor_hwmon(int *tgpu)
{
	char base[128];
	char path[192];
	int t;
	static const char *const names[] = { "nvidia", "amdgpu", NULL };
	int i;

	if (!tgpu || *tgpu > 0)
		return;

	for (i = 0; names[i]; i++) {
		if (find_hwmon(names[i], base, sizeof(base)) != 0)
			continue;
		snprintf(path, sizeof(path), "%s/temp1_input", base);
		if (read_millideg_c(path, &t) == 0 && t > 0) {
			*tgpu = t;
			return;
		}
	}
}

int ps_temps_read(int *cpu_c, int *gpu_c)
{
	int tcpu = -1, tgpu = -1;

	if (!cpu_c || !gpu_c)
		return -EINVAL;

	(void)read_named_hwmon_pair("perfectsense", &tcpu, &tgpu, NULL, NULL);
	if (tcpu < 0 && tgpu < 0)
		(void)read_named_hwmon_pair("acer", &tcpu, &tgpu, NULL, NULL);

	raise_cpu_coretemp(&tcpu);
	try_gpu_vendor_hwmon(&tgpu);

	if (tcpu < 0 && tgpu < 0)
		return -ENODEV;
	*cpu_c = tcpu < 0 ? 0 : tcpu;
	*gpu_c = tgpu < 0 ? 0 : tgpu;
	return 0;
}

int ps_fan_rpm_get(int *cpu_rpm, int *gpu_rpm)
{
	int crpm = -1, grpm = -1;

	if (!cpu_rpm || !gpu_rpm)
		return -EINVAL;

	if (read_named_hwmon_pair("perfectsense", NULL, NULL, &crpm, &grpm) !=
		    0 &&
	    read_named_hwmon_pair("acer", NULL, NULL, &crpm, &grpm) != 0)
		return -ENODEV;

	*cpu_rpm = crpm < 0 ? 0 : crpm;
	*gpu_rpm = grpm < 0 ? 0 : grpm;
	return 0;
}
