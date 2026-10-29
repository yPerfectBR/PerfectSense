/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Per-mode NVIDIA clock offset profile (the Acer PredatorSense "GPU OC" step).
 *
 * PredatorSense 5 applies a core/memory clock offset after each operating
 * mode change. On the PHN16-72 boards (subsystem 1025:1731) only Turbo has a
 * non-zero profile (+100 MHz core, +200 MHz memory); every other mode is 0.
 *
 * The offsets go through NVML (libnvidia-ml.so.1, loaded at run time). NVML is
 * only touched while the dGPU is already awake (runtime_status == "active") and
 * is shut down again right away, so this never wakes a sleeping dGPU and never
 * keeps it from suspending. When the dGPU is asleep the apply returns -EAGAIN
 * and the caller retries later.
 */

#define _POSIX_C_SOURCE 200809L

#include "perfectsense.h"

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PCI_ROOT "/sys/bus/pci/devices"

typedef int nvml_ret_t;
typedef void *nvml_dev_t;

#define NVML_OK 0
#define NVML_CLOCK_GRAPHICS 0
#define NVML_CLOCK_MEM 2
#define NVML_PSTATE_0 0

struct nvml_clock_offset {
	unsigned int version;
	int type;
	int pstate;
	int offset_mhz;
	int min_offset_mhz;
	int max_offset_mhz;
};
#define NVML_CLOCK_OFFSET_V1 ((unsigned int)sizeof(struct nvml_clock_offset) | (1u << 24))

struct oc_profile {
	const char *mode;
	int core_mhz;
	int mem_mhz;
};

static const struct oc_profile g_profiles[] = {
	{ "low-power", 0, 0 },
	{ "quiet", 0, 0 },
	{ "balanced", 0, 0 },
	{ "balanced-performance", 0, 0 },
	{ "performance", 100, 200 },
};

/* Boards PredatorSense ships a profile for (RTX 4050/4060/4070 Max-Q, subsystem 1025:1731). */
static const unsigned g_devices[] = { 0x2860, 0x28e0, 0x28e1 };

static int read_hex(const char *path, unsigned *out)
{
	FILE *f = fopen(path, "r");
	char buf[32];

	if (!f)
		return -errno;
	if (!fgets(buf, sizeof(buf), f)) {
		fclose(f);
		return -EIO;
	}
	fclose(f);
	*out = (unsigned)strtoul(buf, NULL, 16);
	return 0;
}

static int find_gpu(char *addr, size_t addr_len)
{
	DIR *d = opendir(PCI_ROOT);
	struct dirent *e;
	int found = -ENODEV;

	if (!d)
		return -errno;
	while ((e = readdir(d))) {
		char path[512];
		unsigned vendor, cls, sub_vendor, sub_dev, dev;
		size_t i;

		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), PCI_ROOT "/%s/vendor", e->d_name);
		if (read_hex(path, &vendor) || vendor != 0x10de)
			continue;
		snprintf(path, sizeof(path), PCI_ROOT "/%s/class", e->d_name);
		if (read_hex(path, &cls) || (cls >> 16) != 0x03)
			continue;
		snprintf(path, sizeof(path), PCI_ROOT "/%s/subsystem_vendor", e->d_name);
		if (read_hex(path, &sub_vendor) || sub_vendor != 0x1025)
			continue;
		snprintf(path, sizeof(path), PCI_ROOT "/%s/subsystem_device", e->d_name);
		if (read_hex(path, &sub_dev) || sub_dev != 0x1731)
			continue;
		snprintf(path, sizeof(path), PCI_ROOT "/%s/device", e->d_name);
		if (read_hex(path, &dev))
			continue;
		for (i = 0; i < sizeof(g_devices) / sizeof(g_devices[0]); i++) {
			if (dev == g_devices[i]) {
				snprintf(addr, addr_len, "%s", e->d_name);
				found = 0;
				break;
			}
		}
		if (!found)
			break;
	}
	closedir(d);
	return found;
}

static int gpu_awake(const char *addr)
{
	char path[512], buf[32];
	FILE *f;

	snprintf(path, sizeof(path), PCI_ROOT "/%s/power/runtime_status", addr);
	f = fopen(path, "r");
	if (!f)
		return 1; /* no runtime PM info: assume awake */
	if (!fgets(buf, sizeof(buf), f)) {
		fclose(f);
		return 0;
	}
	fclose(f);
	return strncmp(buf, "active", 6) == 0;
}

int ps_gpu_oc_wanted(const char *mode, int *core_mhz, int *mem_mhz)
{
	size_t i;

	if (!mode)
		return -EINVAL;
	for (i = 0; i < sizeof(g_profiles) / sizeof(g_profiles[0]); i++) {
		if (!strcmp(mode, g_profiles[i].mode)) {
			if (core_mhz)
				*core_mhz = g_profiles[i].core_mhz;
			if (mem_mhz)
				*mem_mhz = g_profiles[i].mem_mhz;
			return 0;
		}
	}
	return -EINVAL;
}

struct nvml {
	void *lib;
	nvml_dev_t dev;
	nvml_ret_t (*shutdown)(void);
};

static int nvml_open(struct nvml *n, const char *addr)
{
	nvml_ret_t (*init)(void);
	nvml_ret_t (*by_pci)(const char *, nvml_dev_t *);

	memset(n, 0, sizeof(*n));
	n->lib = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
	if (!n->lib)
		return -ENOENT;
	init = (nvml_ret_t (*)(void))dlsym(n->lib, "nvmlInit_v2");
	by_pci = (nvml_ret_t (*)(const char *, nvml_dev_t *))dlsym(n->lib, "nvmlDeviceGetHandleByPciBusId_v2");
	n->shutdown = (nvml_ret_t (*)(void))dlsym(n->lib, "nvmlShutdown");
	if (!init || !by_pci || !n->shutdown || init() != NVML_OK) {
		dlclose(n->lib);
		n->lib = NULL;
		return -ENOSYS;
	}
	if (by_pci(addr, &n->dev) != NVML_OK) {
		n->shutdown();
		dlclose(n->lib);
		n->lib = NULL;
		return -ENODEV;
	}
	return 0;
}

static void nvml_close(struct nvml *n)
{
	if (!n->lib)
		return;
	n->shutdown();
	dlclose(n->lib);
	n->lib = NULL;
}

static int nvml_get_offsets(struct nvml *n, int *core, int *mem)
{
	nvml_ret_t (*gpc)(nvml_dev_t, int *) =
		(nvml_ret_t (*)(nvml_dev_t, int *))dlsym(n->lib, "nvmlDeviceGetGpcClkVfOffset");
	nvml_ret_t (*mm)(nvml_dev_t, int *) =
		(nvml_ret_t (*)(nvml_dev_t, int *))dlsym(n->lib, "nvmlDeviceGetMemClkVfOffset");

	if (!gpc || !mm)
		return -ENOSYS;
	if (gpc(n->dev, core) != NVML_OK || mm(n->dev, mem) != NVML_OK)
		return -EIO;
	return 0;
}

static int nvml_set_offsets(struct nvml *n, int core, int mem)
{
	nvml_ret_t (*set)(nvml_dev_t, struct nvml_clock_offset *) =
		(nvml_ret_t (*)(nvml_dev_t, struct nvml_clock_offset *))dlsym(n->lib, "nvmlDeviceSetClockOffsets");
	nvml_ret_t (*gpc)(nvml_dev_t, int) =
		(nvml_ret_t (*)(nvml_dev_t, int))dlsym(n->lib, "nvmlDeviceSetGpcClkVfOffset");
	nvml_ret_t (*mm)(nvml_dev_t, int) =
		(nvml_ret_t (*)(nvml_dev_t, int))dlsym(n->lib, "nvmlDeviceSetMemClkVfOffset");

	if (set) {
		struct nvml_clock_offset o;
		nvml_ret_t a, b;

		memset(&o, 0, sizeof(o));
		o.version = NVML_CLOCK_OFFSET_V1;
		o.pstate = NVML_PSTATE_0;
		o.type = NVML_CLOCK_GRAPHICS;
		o.offset_mhz = core;
		a = set(n->dev, &o);
		o.type = NVML_CLOCK_MEM;
		o.offset_mhz = mem;
		b = set(n->dev, &o);
		if (a == NVML_OK && b == NVML_OK)
			return 0;
	}
	if (!gpc || !mm)
		return -ENOSYS;
	if (gpc(n->dev, core) != NVML_OK || mm(n->dev, mem) != NVML_OK)
		return -EIO;
	return 0;
}

int ps_gpu_oc_status(const char *mode, struct ps_gpu_oc_state *out)
{
	char addr[64];
	int err;

	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	out->core_mhz = out->mem_mhz = PS_GPU_OC_UNKNOWN;

	err = find_gpu(addr, sizeof(addr));
	if (err)
		return err;
	out->supported = 1;
	if (mode)
		(void)ps_gpu_oc_wanted(mode, &out->wanted_core_mhz, &out->wanted_mem_mhz);

	out->awake = gpu_awake(addr);
	if (out->awake) {
		struct nvml n;

		if (nvml_open(&n, addr) == 0) {
			int core = 0, mem = 0;

			if (nvml_get_offsets(&n, &core, &mem) == 0) {
				out->core_mhz = core;
				out->mem_mhz = mem;
			}
			nvml_close(&n);
		}
	}
	return 0;
}

int ps_gpu_oc_apply(const char *mode)
{
	struct nvml n;
	char addr[64];
	int core, mem, cur_core = 0, cur_mem = 0, err;

	err = ps_gpu_oc_wanted(mode, &core, &mem);
	if (err)
		return err;
	err = find_gpu(addr, sizeof(addr));
	if (err)
		return err;
	if (!gpu_awake(addr))
		return -EAGAIN;

	err = nvml_open(&n, addr);
	if (err)
		return err;
	if (nvml_get_offsets(&n, &cur_core, &cur_mem) == 0 && cur_core == core && cur_mem == mem) {
		nvml_close(&n);
		return 0;
	}
	err = nvml_set_offsets(&n, core, mem);
	nvml_close(&n);
	return err;
}
