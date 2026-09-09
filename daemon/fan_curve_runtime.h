/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef PERFECTSENSE_FAN_CURVE_RUNTIME_H
#define PERFECTSENSE_FAN_CURVE_RUNTIME_H

#include "perfectsense.h"

struct fc_runtime_status {
	int enabled;
	int active;
	int paused_manual;
	int temp_cpu;
	int temp_gpu;
	unsigned applied_cpu;
	unsigned applied_gpu;
	struct ps_fan_curve_config cfg;
};

int fc_runtime_init(void);
void fc_runtime_shutdown(void);
int fc_runtime_timer_fd(void);
int fc_runtime_enabled(void);
void fc_runtime_get_status(struct fc_runtime_status *st);
int fc_runtime_reload(void);
void fc_runtime_on_timer(void);

#endif
