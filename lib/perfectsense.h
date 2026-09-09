/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef LIBPERFECTSENSE_H
#define LIBPERFECTSENSE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS_SYSFS_DEFAULT "/sys/devices/platform/perfectsense/perfect_sense"
#define PS_KB_SYSFS_DEFAULT "/sys/devices/platform/perfectsense/four_zoned_kb"
#define PS_LOGO_SYSFS_DEFAULT "/sys/devices/platform/perfectsense/back_logo"
#define PS_PLATFORM_PROFILE_PATH "/sys/firmware/acpi/platform_profile"
#define PS_PLATFORM_PROFILE_CHOICES_PATH \
	"/sys/firmware/acpi/platform_profile_choices"

struct ps_status {
	unsigned api_version;
	char thermal_profile[32];
	unsigned thermal_profile_raw;
	int ac_online; /* 0 / 1 */
	char platform_profile[64];
	unsigned fan_duty_cpu;
	unsigned fan_duty_gpu;
	unsigned fan_target_cpu;
	unsigned fan_target_gpu;
	int fan_auto; /* 1 when target is 0,0 */
	int battery_limiter;
	int battery_calibration;
	int usb_charging;
	int lcd_override;
	int backlight_timeout;
	int boot_animation_sound;
	char rgb[64]; /* per_zone_mode string or empty */
	char logo[32]; /* color string or empty */
	char rgb_effect[64]; /* four_zone_mode or empty */
	int temp_cpu; /* °C, or -1 */
	int temp_gpu;
	int fan_rpm_cpu; /* RPM, or -1 */
	int fan_rpm_gpu;
	/*
	 * nvidia-powerd Dynamic Boost vs thermal policy:
	 *   nvidia_powerd:         live unit (0/1), or -1 if unknown
	 *   nvidia_powerd_wanted:  expected for thermal_profile (0/1), or -1
	 *   nvidia_powerd_synced:  1 match, 0 mismatch, -1 unknown
	 * UIs should warn when synced == 0 (often systemd start-limit).
	 */
	int nvidia_powerd;
	int nvidia_powerd_wanted;
	int nvidia_powerd_synced;
};

int ps_available(void);
const char *ps_sysfs_root(void);
const char *ps_kb_sysfs_root(void);
const char *ps_logo_sysfs_root(void);

int ps_status_read(struct ps_status *out);

int ps_mode_get(char *buf, size_t buflen);
int ps_mode_set(const char *name);

/* NVIDIA GPU (Helios): nvidia-powerd Dynamic Boost + optional PowerMizer */
int ps_gpu_powerd_get(int *enabled);
int ps_gpu_powerd_set(int enabled);
int ps_gpu_powerd_for_profile(const char *thermal_name);
/* Wanted state for a thermal name: *wanted = 0|1. */
int ps_gpu_powerd_wanted(const char *thermal_name, int *wanted);
/*
 * Compare live powerd to wanted for thermal_name (NULL = current sysfs mode).
 * *synced 1=ok, 0=mismatch. enabled/wanted may be NULL.
 * Returns 0 on read OK, negative errno on hard failure.
 */
int ps_gpu_powerd_synced(const char *thermal_name, int *synced, int *enabled,
			 int *wanted);

int ps_gpu_powermizer_get(int *mode);
int ps_gpu_powermizer_set(int mode);
int ps_gpu_powermizer_for_profile(const char *thermal_name);
int ps_gpu_powermizer_from_name(const char *name, int *mode);
const char *ps_gpu_powermizer_name(int mode);

int ps_fan_duty_get(unsigned *cpu, unsigned *gpu);
int ps_fan_target_get(unsigned *cpu, unsigned *gpu);
int ps_fan_set(unsigned cpu, unsigned gpu);
int ps_fan_set_auto(void);

int ps_attr_get_int(const char *name, int *value);
int ps_attr_set_int(const char *name, int value);

int ps_rgb_get(char *buf, size_t buflen);
int ps_rgb_set(const char *per_zone_csv);
int ps_rgb_off(void);
int ps_rgb_effect_get(char *buf, size_t buflen);
int ps_rgb_effect_set(const char *effect_csv);
int ps_logo_get(char *buf, size_t buflen);
int ps_logo_set(const char *color_csv);
int ps_logo_off(void);
int ps_lighting_reset(void);
int ps_event_inject(const char *name);

/* Phase 8 — CPU power (userspace sysfs) */
struct ps_cpu_power {
	int available;
	char governor[32];
	char epp[32];
	int no_turbo; /* 0/1, or -1 if unavailable */
	unsigned max_perf_pct;
	unsigned min_perf_pct;
	unsigned scaling_max_khz;
	unsigned scaling_min_khz;
	unsigned cpuinfo_max_khz;
	unsigned long pl1_uw;
	unsigned long pl2_uw;
};

int ps_cpu_power_available(void);
int ps_cpu_power_read(struct ps_cpu_power *out);
int ps_cpu_power_set_governor(const char *name);
int ps_cpu_power_set_epp(const char *name);
int ps_cpu_power_set_turbo(int enabled);
int ps_cpu_power_set_max_perf_pct(unsigned pct);
int ps_cpu_power_set_scaling_max_khz(unsigned khz);
int ps_cpu_power_set_pl1_uw(unsigned long uw);
int ps_cpu_power_set_pl2_uw(unsigned long uw);
int ps_cpu_power_snapshot_save(void);
int ps_cpu_power_snapshot_clear(void);
int ps_cpu_power_restore(void);
/* presets: power|eco, balanced, performance|bp, turbo; sync_platform_profile≠0 links thermal */
int ps_cpu_power_preset(const char *name, int sync_platform_profile);
/* Best-effort CPU knobs for a thermal_profile name (used by ps_mode_set). */
int ps_cpu_power_for_profile(const char *thermal_name);

int ps_temps_read(int *cpu_c, int *gpu_c);
int ps_fan_rpm_get(int *cpu_rpm, int *gpu_rpm);

/* Custom fan curve (temp→duty). Timer in perfectsensed when enabled. */
#define PS_FAN_CURVE_MAX_POINTS 16
struct ps_fan_curve_point {
	int temp_c;
	unsigned cpu_pct;
	unsigned gpu_pct;
};
struct ps_fan_curve_config {
	int enabled;
	int interval_ms;
	int n_points;
	struct ps_fan_curve_point points[PS_FAN_CURVE_MAX_POINTS];
};
void ps_fan_curve_defaults(struct ps_fan_curve_config *cfg);
int ps_fan_curve_config_load(struct ps_fan_curve_config *cfg);
int ps_fan_curve_config_save(const struct ps_fan_curve_config *cfg);
int ps_fan_curve_parse_points(const char *csv, struct ps_fan_curve_config *cfg);
void ps_fan_curve_eval(const struct ps_fan_curve_config *cfg, int cpu_c,
		       int gpu_c, unsigned *out_cpu, unsigned *out_gpu);

const char *ps_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* LIBPERFECTSENSE_H */
