/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * PerfectSense — userspace library over sysfs perfect_sense ABI.
 */

#include "perfectsense.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
			 buf[n - 1] == ' ' || buf[n - 1] == '\t')) {
		buf[--n] = '\0';
	}

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

static void path_join(char *out, size_t outlen, const char *root,
		      const char *leaf)
{
	snprintf(out, outlen, "%s/%s", root, leaf);
}

const char *ps_sysfs_root(void)
{
	static char envbuf[512];
	const char *env = getenv("PERFECTSENSE_SYSFS");

	if (env && env[0]) {
		snprintf(envbuf, sizeof(envbuf), "%s", env);
		return envbuf;
	}
	return PS_SYSFS_DEFAULT;
}

int ps_available(void)
{
	char path[512];

	path_join(path, sizeof(path), ps_sysfs_root(), "api_version");
	return access(path, R_OK) == 0 ? 0 : -ENODEV;
}

static int normalize_mode_alias(const char *in, char *out, size_t outlen)
{
	if (!in || !out || outlen == 0)
		return -EINVAL;

	/* Friendly aliases → sysfs thermal_profile names */
	if (!strcmp(in, "eco"))
		in = "low-power";
	else if (!strcmp(in, "turbo"))
		in = "performance";
	else if (!strcmp(in, "bp") || !strcmp(in, "performance-balanced"))
		in = "balanced-performance";

	snprintf(out, outlen, "%s", in);
	return 0;
}

int ps_status_read(struct ps_status *out)
{
	char path[512];
	char buf[128];
	unsigned u1, u2;
	int err;

	if (!out)
		return -EINVAL;

	memset(out, 0, sizeof(*out));

	err = ps_available();
	if (err)
		return err;

	path_join(path, sizeof(path), ps_sysfs_root(), "api_version");
	err = read_trimmed(path, buf, sizeof(buf));
	if (err)
		return err;
	if (sscanf(buf, "%u", &out->api_version) != 1)
		return -EINVAL;

	path_join(path, sizeof(path), ps_sysfs_root(), "thermal_profile");
	err = read_trimmed(path, out->thermal_profile,
			   sizeof(out->thermal_profile));
	if (err)
		return err;

	path_join(path, sizeof(path), ps_sysfs_root(), "thermal_profile_raw");
	err = read_trimmed(path, buf, sizeof(buf));
	if (err)
		return err;
	{
		char *end = NULL;
		unsigned long raw = strtoul(buf, &end, 0);

		if (end == buf || *end != '\0' || raw > 0xff)
			return -EINVAL;
		out->thermal_profile_raw = (unsigned)raw;
	}

	path_join(path, sizeof(path), ps_sysfs_root(), "ac_online");
	err = read_trimmed(path, buf, sizeof(buf));
	if (err)
		return err;
	out->ac_online = (buf[0] == '1') ? 1 : 0;

	path_join(path, sizeof(path), ps_sysfs_root(), "fan_duty");
	err = read_trimmed(path, buf, sizeof(buf));
	if (err)
		return err;
	if (sscanf(buf, "%u,%u", &u1, &u2) != 2)
		return -EINVAL;
	out->fan_duty_cpu = u1;
	out->fan_duty_gpu = u2;

	path_join(path, sizeof(path), ps_sysfs_root(), "fan_speed");
	err = read_trimmed(path, buf, sizeof(buf));
	if (err)
		return err;
	if (sscanf(buf, "%u,%u", &u1, &u2) != 2)
		return -EINVAL;
	out->fan_target_cpu = u1;
	out->fan_target_gpu = u2;
	out->fan_auto = (u1 == 0 && u2 == 0) ? 1 : 0;

	/* Best-effort; missing platform_profile is non-fatal for status */
	if (read_trimmed(PS_PLATFORM_PROFILE_PATH, out->platform_profile,
			 sizeof(out->platform_profile)) != 0)
		snprintf(out->platform_profile, sizeof(out->platform_profile),
			 "%s", out->thermal_profile);

	return 0;
}

int ps_mode_get(char *buf, size_t buflen)
{
	char path[512];

	if (!buf || buflen == 0)
		return -EINVAL;

	path_join(path, sizeof(path), ps_sysfs_root(), "thermal_profile");
	return read_trimmed(path, buf, buflen);
}

int ps_mode_set(const char *name)
{
	char path[512];
	char normalized[32];
	int err;

	err = normalize_mode_alias(name, normalized, sizeof(normalized));
	if (err)
		return err;

	path_join(path, sizeof(path), ps_sysfs_root(), "thermal_profile");
	return write_text(path, normalized);
}

int ps_fan_duty_get(unsigned *cpu, unsigned *gpu)
{
	char path[512];
	char buf[64];
	unsigned u1, u2;
	int err;

	if (!cpu || !gpu)
		return -EINVAL;

	path_join(path, sizeof(path), ps_sysfs_root(), "fan_duty");
	err = read_trimmed(path, buf, sizeof(buf));
	if (err)
		return err;
	if (sscanf(buf, "%u,%u", &u1, &u2) != 2)
		return -EINVAL;
	*cpu = u1;
	*gpu = u2;
	return 0;
}

int ps_fan_target_get(unsigned *cpu, unsigned *gpu)
{
	char path[512];
	char buf[64];
	unsigned u1, u2;
	int err;

	if (!cpu || !gpu)
		return -EINVAL;

	path_join(path, sizeof(path), ps_sysfs_root(), "fan_speed");
	err = read_trimmed(path, buf, sizeof(buf));
	if (err)
		return err;
	if (sscanf(buf, "%u,%u", &u1, &u2) != 2)
		return -EINVAL;
	*cpu = u1;
	*gpu = u2;
	return 0;
}

int ps_fan_set(unsigned cpu, unsigned gpu)
{
	char path[512];
	char buf[32];

	if (cpu > 100 || gpu > 100)
		return -EINVAL;

	path_join(path, sizeof(path), ps_sysfs_root(), "fan_speed");
	snprintf(buf, sizeof(buf), "%u,%u", cpu, gpu);
	return write_text(path, buf);
}

int ps_fan_set_auto(void)
{
	return ps_fan_set(0, 0);
}

const char *ps_strerror(int err)
{
	if (err >= 0)
		return "success";
	switch (-err) {
	case ENODEV:
		return "perfectsense sysfs not found (is the module loaded?)";
	case EACCES:
	case EPERM:
		return "permission denied (sysfs write needs root or group)";
	case EINVAL:
		return "invalid argument";
	case EOPNOTSUPP:
		return "operation not supported (often AC policy)";
	default:
		return strerror(-err);
	}
}
