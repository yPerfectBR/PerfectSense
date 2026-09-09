/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef LIBPERFECTSENSE_H
#define LIBPERFECTSENSE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PS_SYSFS_DEFAULT "/sys/devices/platform/perfectsense/perfect_sense"
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
};

/* 0 = module sysfs present */
int ps_available(void);

/* Resolve sysfs root (default or PERFECTSENSE_SYSFS). Returns static buffer. */
const char *ps_sysfs_root(void);

int ps_status_read(struct ps_status *out);

int ps_mode_get(char *buf, size_t buflen);
int ps_mode_set(const char *name);

int ps_fan_duty_get(unsigned *cpu, unsigned *gpu);
int ps_fan_target_get(unsigned *cpu, unsigned *gpu);
int ps_fan_set(unsigned cpu, unsigned gpu);
int ps_fan_set_auto(void);

const char *ps_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* LIBPERFECTSENSE_H */
