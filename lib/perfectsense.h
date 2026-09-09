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
int ps_logo_get(char *buf, size_t buflen);
int ps_logo_set(const char *color_csv);
int ps_logo_off(void);
int ps_lighting_reset(void);

const char *ps_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* LIBPERFECTSENSE_H */
