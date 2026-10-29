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

/* Forward decls used by ps_status_read */
int ps_attr_get_int(const char *name, int *value);
int ps_attr_set_int(const char *name, int value);
int ps_rgb_get(char *buf, size_t buflen);
int ps_rgb_effect_get(char *buf, size_t buflen);
int ps_logo_get(char *buf, size_t buflen);

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

const char *ps_kb_sysfs_root(void)
{
	static char envbuf[512];
	const char *env = getenv("PERFECTSENSE_KB_SYSFS");

	if (env && env[0]) {
		snprintf(envbuf, sizeof(envbuf), "%s", env);
		return envbuf;
	}
	return PS_KB_SYSFS_DEFAULT;
}

const char *ps_logo_sysfs_root(void)
{
	static char envbuf[512];
	const char *env = getenv("PERFECTSENSE_LOGO_SYSFS");

	if (env && env[0]) {
		snprintf(envbuf, sizeof(envbuf), "%s", env);
		return envbuf;
	}
	return PS_LOGO_SYSFS_DEFAULT;
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

	out->battery_limiter = -1;
	out->battery_calibration = -1;
	out->usb_charging = -1;
	out->lcd_override = -1;
	out->backlight_timeout = -1;
	out->boot_animation_sound = -1;

	(void)ps_attr_get_int("battery_limiter", &out->battery_limiter);
	(void)ps_attr_get_int("battery_calibration", &out->battery_calibration);
	(void)ps_attr_get_int("usb_charging", &out->usb_charging);
	(void)ps_attr_get_int("lcd_override", &out->lcd_override);
	(void)ps_attr_get_int("backlight_timeout", &out->backlight_timeout);
	(void)ps_attr_get_int("boot_animation_sound",
			      &out->boot_animation_sound);

	out->rgb[0] = '\0';
	out->logo[0] = '\0';
	out->rgb_effect[0] = '\0';
	out->temp_cpu = -1;
	out->temp_gpu = -1;
	out->fan_rpm_cpu = -1;
	out->fan_rpm_gpu = -1;
	(void)ps_rgb_get(out->rgb, sizeof(out->rgb));
	(void)ps_rgb_effect_get(out->rgb_effect, sizeof(out->rgb_effect));
	(void)ps_logo_get(out->logo, sizeof(out->logo));
	{
		int tc = -1, tg = -1, rc = -1, rg = -1;

		if (ps_temps_read(&tc, &tg) == 0) {
			out->temp_cpu = tc;
			out->temp_gpu = tg;
		}
		if (ps_fan_rpm_get(&rc, &rg) == 0) {
			out->fan_rpm_cpu = rc;
			out->fan_rpm_gpu = rg;
		}
	}

	out->nvidia_powerd = -1;
	out->nvidia_powerd_wanted = -1;
	out->nvidia_powerd_synced = -1;
	{
		int synced = 0, on = 0, want = 0;

		if (ps_gpu_powerd_synced(out->thermal_profile, &synced, &on,
					 &want) == 0) {
			out->nvidia_powerd = on;
			out->nvidia_powerd_wanted = want;
			out->nvidia_powerd_synced = synced;
		}
	}

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

	/*
	 * Acer rule (PredatorSense UI): entering or leaving Quiet puts the fans
	 * back to firmware Auto, so a manual duty set for another mode does not
	 * fight Quiet's low-noise curve.
	 */
	{
		char prev[32] = "";
		int to_quiet = !strcmp(normalized, "quiet");

		(void)ps_mode_get(prev, sizeof(prev));
		if (strcmp(prev, normalized) && (to_quiet || !strcmp(prev, "quiet")))
			(void)ps_fan_set_auto();
	}

	path_join(path, sizeof(path), ps_sysfs_root(), "thermal_profile");
	err = write_text(path, normalized);
	if (err)
		return err;

	/*
	 * Helios: sync nvidia-powerd (Dynamic Boost ~80W↔~140W).
	 * Thermal write already succeeded. -EBUSY means powerd did not reach
	 * the expected state (UI should warn; profile is still applied).
	 */
	err = ps_gpu_powerd_for_profile(normalized);
	if (err && err != -EBUSY)
		return err;

	/*
	 * Best-effort CPU governor / EPP for the same profile. Failure here
	 * must not undo thermal/powerd — ignore -ENODEV and write errors.
	 */
	(void)ps_cpu_power_for_profile(normalized);

	/*
	 * Acer GPU clock profile for the new mode. -EAGAIN (dGPU asleep) is not an
	 * error here: perfectsensed retries once the dGPU wakes up on its own.
	 */
	(void)ps_gpu_oc_apply(normalized);

	return err; /* 0 or -EBUSY from powerd */
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

static int attr_name_ok(const char *name)
{
	static const char *const allowed[] = {
		"battery_limiter",
		"battery_calibration",
		"usb_charging",
		"lcd_override",
		"backlight_timeout",
		"boot_animation_sound",
		NULL,
	};
	int i;

	if (!name)
		return 0;
	for (i = 0; allowed[i]; i++) {
		if (!strcmp(name, allowed[i]))
			return 1;
	}
	return 0;
}

int ps_attr_get_int(const char *name, int *value)
{
	char path[512];
	char buf[64];
	char *end;
	long v;
	int err;

	if (!value || !attr_name_ok(name))
		return -EINVAL;

	path_join(path, sizeof(path), ps_sysfs_root(), name);
	err = read_trimmed(path, buf, sizeof(buf));
	if (err)
		return err;

	v = strtol(buf, &end, 0);
	if (end == buf || *end != '\0')
		return -EINVAL;
	if (v < -1 || v > 100)
		return -EINVAL;

	*value = (int)v;
	return 0;
}

int ps_attr_set_int(const char *name, int value)
{
	char path[512];
	char buf[32];

	if (!attr_name_ok(name))
		return -EINVAL;

	path_join(path, sizeof(path), ps_sysfs_root(), name);
	snprintf(buf, sizeof(buf), "%d", value);
	return write_text(path, buf);
}

int ps_rgb_get(char *buf, size_t buflen)
{
	char path[512];

	if (!buf || buflen == 0)
		return -EINVAL;
	path_join(path, sizeof(path), ps_kb_sysfs_root(), "per_zone_mode");
	return read_trimmed(path, buf, buflen);
}

int ps_rgb_set(const char *per_zone_csv)
{
	char path[512];

	if (!per_zone_csv)
		return -EINVAL;
	path_join(path, sizeof(path), ps_kb_sysfs_root(), "per_zone_mode");
	return write_text(path, per_zone_csv);
}

int ps_rgb_off(void)
{
	return ps_rgb_set("000000,000000,000000,000000,0");
}

int ps_rgb_effect_get(char *buf, size_t buflen)
{
	char path[512];

	if (!buf || buflen == 0)
		return -EINVAL;
	path_join(path, sizeof(path), ps_kb_sysfs_root(), "four_zone_mode");
	return read_trimmed(path, buf, buflen);
}

int ps_rgb_effect_set(const char *effect_csv)
{
	char path[512];

	if (!effect_csv)
		return -EINVAL;
	path_join(path, sizeof(path), ps_kb_sysfs_root(), "four_zone_mode");
	return write_text(path, effect_csv);
}

int ps_logo_get(char *buf, size_t buflen)
{
	char path[512];

	if (!buf || buflen == 0)
		return -EINVAL;
	path_join(path, sizeof(path), ps_logo_sysfs_root(), "color");
	return read_trimmed(path, buf, buflen);
}

int ps_logo_set(const char *color_csv)
{
	char path[512];

	if (!color_csv)
		return -EINVAL;
	path_join(path, sizeof(path), ps_logo_sysfs_root(), "color");
	return write_text(path, color_csv);
}

int ps_logo_off(void)
{
	return ps_logo_set("000000,0,0");
}

int ps_lighting_reset(void)
{
	char path[512];

	path_join(path, sizeof(path), ps_sysfs_root(), "lighting_reset");
	return write_text(path, "1");
}

int ps_event_inject(const char *name)
{
	char path[512];

	if (!name || !*name)
		return -EINVAL;
	path_join(path, sizeof(path), ps_sysfs_root(), "inject_event");
	return write_text(path, name);
}

const char *ps_strerror(int err)
{
	if (err >= 0)
		return "success";
	switch (-err) {
	case EAGAIN:
		return "NVIDIA PowerMizer did not stick (NV-CONTROL/Wayland?)";
	case EBUSY:
		return "nvidia-powerd not in expected state (start-limit/failed? "
		       "systemctl reset-failed nvidia-powerd.service && "
		       "systemctl start nvidia-powerd.service)";
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
