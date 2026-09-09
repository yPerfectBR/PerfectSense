/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef FAN_AGGRESSOR_RUNTIME_H
#define FAN_AGGRESSOR_RUNTIME_H

#include "perfectsense.h"

struct fa_runtime_status {
	int enabled;
	int active;
	int paused_manual;
	int temp_cpu;
	int temp_gpu;
	unsigned native_cpu;
	unsigned native_gpu;
	unsigned applied_cpu;
	unsigned applied_gpu;
	struct ps_fan_aggressor_config cfg;
};

int fa_runtime_init(void);
void fa_runtime_shutdown(void);
int fa_runtime_timer_fd(void);
int fa_runtime_enabled(void);
int fa_runtime_active(void);
void fa_runtime_get_config(struct ps_fan_aggressor_config *cfg);
void fa_runtime_get_status(struct fa_runtime_status *st);
int fa_runtime_set_enabled(int on);
int fa_runtime_set_config(const struct ps_fan_aggressor_config *cfg);
void fa_runtime_on_timer(void);

#endif
