/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Temperature helpers (acer hwmon + coretemp package).
 */

#include "perfectsense.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_millideg_c(const char *path, int *out_c)
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

int ps_temps_read(int *cpu_c, int *gpu_c)
{
	char acer[128];
	char path[192];
	int tcpu = -1, tgpu = -1, t;
	DIR *d;
	struct dirent *de;

	if (!cpu_c || !gpu_c)
		return -EINVAL;

	if (find_hwmon("acer", acer, sizeof(acer)) == 0) {
		snprintf(path, sizeof(path), "%s/temp1_input", acer);
		if (read_millideg_c(path, &t) == 0)
			tcpu = t;
		snprintf(path, sizeof(path), "%s/temp2_input", acer);
		if (read_millideg_c(path, &t) == 0)
			tgpu = t;
	}

	/* Raise CPU with coretemp Package id 0 when available */
	d = opendir("/sys/class/hwmon");
	if (d) {
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
			if (read_millideg_c(path, &t) == 0 && t > tcpu)
				tcpu = t;
			break;
		}
		closedir(d);
	}

	if (tcpu < 0 && tgpu < 0)
		return -ENODEV;
	*cpu_c = tcpu < 0 ? 0 : tcpu;
	*gpu_c = tgpu < 0 ? 0 : tgpu;
	return 0;
}
