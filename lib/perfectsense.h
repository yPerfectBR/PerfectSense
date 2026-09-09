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
};

int ps_available(void);
const char *ps_sysfs_root(void);
const char *ps_kb_sysfs_root(void);
const char *ps_logo_sysfs_root(void);

int ps_status_read(struct ps_status *out);

int ps_mode_get(char *buf, size_t buflen);
int ps_mode_set(const char *name);

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

/* Phase 9 — FanAggressor (config shared; timer in daemon) */
struct ps_fan_aggressor_config {
	int enabled;
	int engage_c;
	int hysteresis_c;
	int hold_sec;
	int interval_ms;
	int offset_cpu;
	int offset_gpu;
	int floor_cpu;
	int floor_gpu;
};

int ps_temps_read(int *cpu_c, int *gpu_c);
int ps_fan_rpm_get(int *cpu_rpm, int *gpu_rpm);
void ps_fan_aggressor_defaults(struct ps_fan_aggressor_config *cfg);
int ps_fan_aggressor_config_load(struct ps_fan_aggressor_config *cfg);
int ps_fan_aggressor_config_save(const struct ps_fan_aggressor_config *cfg);
void ps_fan_aggressor_compute_duty(const struct ps_fan_aggressor_config *cfg,
				   unsigned native_cpu, unsigned native_gpu,
				   unsigned *out_cpu, unsigned *out_gpu);

const char *ps_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* LIBPERFECTSENSE_H */
