/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * HDMI / dGPU display policy for the Umbriel session.
 *
 * On the PHN16-72 the HDMI and USB-C DP outputs are wired to the NVIDIA dGPU.
 * While the compositor keeps that GPU open for those outputs, RTD3 can never
 * power it down. "HDMI off" writes an Umbriel include that leaves the dGPU
 * unopened ([drm] ignored_pci_addresses), so it sleeps in D3cold; "HDMI on"
 * clears it. The CLI asks Umbriel to reload its config, and Umbriel opens or
 * releases the dGPU on the spot (the Pandora fork); an Umbriel that reads [drm]
 * only at startup applies it on the next session start.
 *
 * Nothing here touches the dGPU itself: the PCI address comes from sysfs IDs
 * and runtime_status, neither of which resumes a suspended device.
 */

#define _POSIX_C_SOURCE 200809L

#include "perfectsense.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define PCI_ROOT "/sys/bus/pci/devices"
#define HDMI_INCLUDE "perfect-sense-gpu.toml"
#define HDMI_MARKER "# perfect-sense-hdmi="

static int read_hex(const char *path, unsigned *out)
{
	char buf[32];
	FILE *f = fopen(path, "r");

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

/* First NVIDIA display-class PCI function (0x03xxxx). */
static int find_dgpu(char *addr, size_t addr_len)
{
	DIR *d = opendir(PCI_ROOT);
	struct dirent *e;
	int found = -ENODEV;

	if (!d)
		return -errno;
	while ((e = readdir(d))) {
		char path[512];
		unsigned vendor, cls;

		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof(path), PCI_ROOT "/%s/vendor", e->d_name);
		if (read_hex(path, &vendor) || vendor != 0x10de)
			continue;
		snprintf(path, sizeof(path), PCI_ROOT "/%s/class", e->d_name);
		if (read_hex(path, &cls) || (cls >> 16) != 0x03)
			continue;
		snprintf(addr, addr_len, "%s", e->d_name);
		found = 0;
		break;
	}
	closedir(d);
	return found;
}

static int include_path(char *out, size_t len)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");

	if (xdg && *xdg)
		snprintf(out, len, "%s/umbriel/" HDMI_INCLUDE, xdg);
	else if (home && *home)
		snprintf(out, len, "%s/.config/umbriel/" HDMI_INCLUDE, home);
	else
		return -ENOENT;
	return 0;
}

/* 1 on, 0 off. A missing or unmarked include means Umbriel's default: on. */
static int read_configured(void)
{
	char path[PATH_MAX], line[256];
	FILE *f;
	int on = 1;

	if (include_path(path, sizeof(path)))
		return 1;
	f = fopen(path, "r");
	if (!f)
		return 1;
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, HDMI_MARKER, strlen(HDMI_MARKER))) {
			on = strncmp(line + strlen(HDMI_MARKER), "off", 3) != 0;
			break;
		}
	}
	fclose(f);
	return on;
}

/* Whether an open file's target is one of the dGPU's DRM nodes (/dev/dri/cardN, renderDN). */
static int is_dgpu_drm_node(const char *pci, const char *target)
{
	char path[PATH_MAX];
	const char *name;
	DIR *drm;
	struct dirent *e;
	int found = 0;

	if (!pci || !pci[0] || strncmp(target, "/dev/dri/", 9))
		return 0;
	name = target + 9;
	snprintf(path, sizeof(path), PCI_ROOT "/%s/drm", pci);
	drm = opendir(path);
	if (!drm)
		return 0;
	while ((e = readdir(drm)))
		if (!strcmp(e->d_name, name)) {
			found = 1;
			break;
		}
	closedir(drm);
	return found;
}

/* 1 when a running umbriel holds the dGPU (its DRM node or an NVIDIA device), 0 when not,
 * -1 when none runs. */
static int session_holds_dgpu(const char *pci)
{
	DIR *proc = opendir("/proc");
	struct dirent *p;
	int result = -1;

	if (!proc)
		return -1;
	while ((p = readdir(proc)) && result != 1) {
		char path[PATH_MAX], comm[64];
		FILE *f;
		DIR *fds;
		struct dirent *fd;

		if (p->d_name[0] < '0' || p->d_name[0] > '9')
			continue;
		snprintf(path, sizeof(path), "/proc/%s/comm", p->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		if (!fgets(comm, sizeof(comm), f)) {
			fclose(f);
			continue;
		}
		fclose(f);
		if (strcmp(comm, "umbriel\n"))
			continue;
		snprintf(path, sizeof(path), "/proc/%s/fd", p->d_name);
		fds = opendir(path);
		if (!fds)
			continue;
		result = 0;
		while ((fd = readdir(fds))) {
			char link[PATH_MAX], target[PATH_MAX];
			ssize_t n;

			if (fd->d_name[0] == '.')
				continue;
			snprintf(link, sizeof(link), "/proc/%s/fd/%s", p->d_name, fd->d_name);
			n = readlink(link, target, sizeof(target) - 1);
			if (n <= 0)
				continue;
			target[n] = '\0';
			if (!strncmp(target, "/dev/nvidia", 11) || is_dgpu_drm_node(pci, target)) {
				result = 1;
				break;
			}
		}
		closedir(fds);
	}
	closedir(proc);
	return result;
}

static int dgpu_awake(const char *addr)
{
	char path[512], buf[32];
	FILE *f;

	snprintf(path, sizeof(path), PCI_ROOT "/%s/power/runtime_status", addr);
	f = fopen(path, "r");
	if (!f)
		return -1;
	if (!fgets(buf, sizeof(buf), f)) {
		fclose(f);
		return -1;
	}
	fclose(f);
	return strncmp(buf, "active", 6) == 0;
}

int ps_hdmi_status(struct ps_hdmi_state *out)
{
	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	out->configured = read_configured();
	if (find_dgpu(out->pci, sizeof(out->pci)) == 0)
		out->dgpu_awake = dgpu_awake(out->pci);
	else
		out->dgpu_awake = -1;
	out->session = session_holds_dgpu(out->pci);
	return 0;
}

int ps_hdmi_set(int on)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8], pci[32];
	FILE *f;
	int err;

	err = include_path(path, sizeof(path));
	if (err)
		return err;
	if (!on && find_dgpu(pci, sizeof(pci)))
		return -ENODEV;
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	f = fopen(tmp, "w");
	if (!f)
		return -errno;
	fputs("# Managed by Perfect Sense (perfectsense hdmi on|off). Do not edit.\n", f);
	fprintf(f, HDMI_MARKER "%s\n", on ? "on" : "off");
	/* The main config declares [drm] too, so this include must always exist. */
	fputs("[drm]\n", f);
	if (!on)
		fprintf(f, "ignored_pci_addresses = [\"%s\"]\n", pci);
	if (fclose(f)) {
		err = -errno;
		unlink(tmp);
		return err;
	}
	if (rename(tmp, path)) {
		err = -errno;
		unlink(tmp);
		return err;
	}
	return 0;
}
