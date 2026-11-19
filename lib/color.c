/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * Internal panel color: gamut mapping for untagged sRGB content.
 *
 * The PHN16-72 panel (and most wide-gamut laptop panels) shows every signal
 * value in its own primaries, so ordinary sRGB content comes out oversaturated.
 * The fix is a 3x3 gamut conversion during composition, and only the compositor
 * can do it: the wlr-gamma-control protocol carries per-channel curves only, and
 * the KMS CTM belongs to the DRM master. Perfect Sense therefore owns the choice
 * and the characterization (panel EDID, or an ICC profile) and hands it to the
 * running compositor through that compositor's own mechanism:
 *
 *   umbriel   include perfect-sense-color.toml (color_profile + sdr_gamut), reloaded live
 *   sway      swaymsg output <name> color_profile icc <file>
 *   KDE       kscreen-doctor output.<name>.colorProfileSource / iccprofile
 *   others    the generated ICC file, to load by hand
 *
 * The choice persists in ~/.config/perfect-sense/color.conf; `perfectsense color
 * apply` re-applies it (for compositors whose runtime setting does not persist).
 */

#define _GNU_SOURCE

#include "perfectsense.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <lcms2.h>
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define DRM_ROOT "/sys/class/drm"
#define UMBRIEL_INCLUDE "perfect-sense-color.toml"

/* ---------------------------------------------------------------- EDID --- */

static const double SRGB_XY[3][2] = { { 0.640, 0.330 }, { 0.300, 0.600 }, { 0.150, 0.060 } };
static const double P3_XY[3][2] = { { 0.680, 0.320 }, { 0.265, 0.690 }, { 0.150, 0.060 } };

static double chroma(unsigned high, unsigned low2)
{
	return (double)((high << 2) | (low2 & 3)) / 1024.0;
}

/* An EDID descriptor's 13-byte text, up to the newline, trailing blanks trimmed. */
static void copy_text(const unsigned char *text, char *out, size_t len)
{
	size_t n;

	for (n = 0; n < 13 && n + 1 < len && text[n] != '\n' && text[n]; n++)
		out[n] = (char)(text[n] >= 0x20 && text[n] < 0x7f ? text[n] : '?');
	out[n] = '\0';
	while (n && out[n - 1] == ' ')
		out[--n] = '\0';
}

int ps_color_parse_edid(const unsigned char *edid, size_t len, struct ps_color_panel *out)
{
	static const unsigned char magic[8] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
	unsigned vendor;
	const unsigned char *d;
	int i;

	if (!edid || !out || len < 128 || memcmp(edid, magic, sizeof(magic)))
		return -EINVAL;
	memset(out->vendor, 0, sizeof(out->vendor));
	memset(out->model, 0, sizeof(out->model));

	vendor = ((unsigned)edid[8] << 8) | edid[9];
	out->vendor[0] = (char)('A' - 1 + ((vendor >> 10) & 0x1f));
	out->vendor[1] = (char)('A' - 1 + ((vendor >> 5) & 0x1f));
	out->vendor[2] = (char)('A' - 1 + (vendor & 0x1f));
	out->product = (unsigned)edid[10] | ((unsigned)edid[11] << 8);

	out->gamma = edid[23] == 0xff ? 2.2 : (edid[23] + 100) / 100.0;

	/* Bytes 25/26 hold the two low bits, 27..34 the high eight. */
	out->red[0] = chroma(edid[27], edid[25] >> 6);
	out->red[1] = chroma(edid[28], edid[25] >> 4);
	out->green[0] = chroma(edid[29], edid[25] >> 2);
	out->green[1] = chroma(edid[30], edid[25]);
	out->blue[0] = chroma(edid[31], edid[26] >> 6);
	out->blue[1] = chroma(edid[32], edid[26] >> 4);
	out->white[0] = chroma(edid[33], edid[26] >> 2);
	out->white[1] = chroma(edid[34], edid[26]);

	/* Model: the display product name (0xFC), else the last unspecified text (0xFE). */
	for (i = 0; i < 4; i++) {
		d = edid + 54 + 18 * i;
		if (!d[0] && !d[1] && d[3] == 0xfe)
			copy_text(d + 5, out->model, sizeof(out->model));
	}
	for (i = 0; i < 4; i++) {
		d = edid + 54 + 18 * i;
		if (!d[0] && !d[1] && d[3] == 0xfc)
			copy_text(d + 5, out->model, sizeof(out->model));
	}

	if (out->red[0] <= 0 || out->green[1] <= 0 || out->blue[0] <= 0 || out->white[1] <= 0)
		return -ENODATA;
	out->coverage_srgb = ps_color_coverage(out, SRGB_XY);
	out->coverage_p3 = ps_color_coverage(out, P3_XY);
	out->wide = out->coverage_srgb >= 0.99 && out->coverage_p3 >= 0.85;
	return 0;
}

/* ------------------------------------------------- gamut coverage (xy) --- */

struct pt {
	double x, y;
};

static double cross(struct pt o, struct pt a, struct pt b)
{
	return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

static double area(const struct pt *p, int n)
{
	double s = 0;
	int i;

	for (i = 0; i < n; i++)
		s += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
	return fabs(s) / 2.0;
}

/* Sutherland–Hodgman: clip polygon `p` by the counter-clockwise triangle `t`. */
static int clip(struct pt *p, int n, const struct pt t[3])
{
	struct pt in[16], out[16];
	int e, i, m;

	memcpy(in, p, sizeof(*p) * (size_t)n);
	for (e = 0; e < 3 && n > 0; e++) {
		struct pt a = t[e], b = t[(e + 1) % 3];

		m = 0;
		for (i = 0; i < n; i++) {
			struct pt cur = in[i], prev = in[(i + n - 1) % n];
			double dc = cross(a, b, cur), dp = cross(a, b, prev);

			if ((dc >= 0) != (dp >= 0) && m < 16) {
				double k = dp / (dp - dc);

				out[m++] = (struct pt){ prev.x + k * (cur.x - prev.x),
							prev.y + k * (cur.y - prev.y) };
			}
			if (dc >= 0 && m < 16)
				out[m++] = cur;
		}
		n = m;
		memcpy(in, out, sizeof(*out) * (size_t)n);
	}
	memcpy(p, in, sizeof(*p) * (size_t)n);
	return n;
}

static void ccw(struct pt t[3])
{
	if (cross(t[0], t[1], t[2]) < 0) {
		struct pt tmp = t[1];

		t[1] = t[2];
		t[2] = tmp;
	}
}

/* Share of the reference triangle that the panel covers, in CIE 1931 xy. */
double ps_color_coverage(const struct ps_color_panel *panel, const double ref[3][2])
{
	struct pt r[16] = { { ref[0][0], ref[0][1] }, { ref[1][0], ref[1][1] }, { ref[2][0], ref[2][1] } };
	struct pt t[3] = { { panel->red[0], panel->red[1] },
			   { panel->green[0], panel->green[1] },
			   { panel->blue[0], panel->blue[1] } };
	double whole;
	int n;

	ccw(t);
	ccw(r);
	whole = area(r, 3);
	if (whole <= 0)
		return 0;
	n = clip(r, 3, t);
	return n >= 3 ? area(r, n) / whole : 0;
}

/* --------------------------------------------------------------- panel --- */

static int read_file(const char *path, unsigned char *buf, size_t cap, size_t *len)
{
	FILE *f = fopen(path, "rb");

	if (!f)
		return -errno;
	*len = fread(buf, 1, cap, f);
	fclose(f);
	return 0;
}

/* The connected internal panel (eDP, then LVDS/DSI): connector name as compositors name it. */
int ps_color_panel(struct ps_color_panel *out)
{
	static const char *const kinds[] = { "-eDP-", "-LVDS-", "-DSI-" };
	DIR *d;
	struct dirent *e;
	size_t k;

	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	for (k = 0; k < sizeof(kinds) / sizeof(*kinds); k++) {
		d = opendir(DRM_ROOT);
		if (!d)
			return -errno;
		while ((e = readdir(d))) {
			char path[PATH_MAX], status[32] = "";
			unsigned char edid[512];
			size_t len = 0;
			const char *name = strstr(e->d_name, kinds[k]);
			FILE *f;

			if (!name || strncmp(e->d_name, "card", 4))
				continue;
			snprintf(path, sizeof(path), DRM_ROOT "/%s/status", e->d_name);
			f = fopen(path, "r");
			if (!f)
				continue;
			if (!fgets(status, sizeof(status), f))
				status[0] = '\0';
			fclose(f);
			if (strncmp(status, "connected", 9))
				continue;
			snprintf(path, sizeof(path), DRM_ROOT "/%s/edid", e->d_name);
			if (read_file(path, edid, sizeof(edid), &len) || len < 128)
				continue;
			if (ps_color_parse_edid(edid, len, out))
				continue;
			snprintf(out->connector, sizeof(out->connector), "%s", name + 1);
			closedir(d);
			return 0;
		}
		closedir(d);
	}
	return -ENODEV;
}

/* ---------------------------------------------------------- persistence --- */

static int config_dir(char *out, size_t len, const char *xdg_var, const char *fallback, const char *leaf)
{
	const char *xdg = getenv(xdg_var);
	const char *home = getenv("HOME");

	if (xdg && *xdg)
		snprintf(out, len, "%s/%s", xdg, leaf);
	else if (home && *home)
		snprintf(out, len, "%s/%s/%s", home, fallback, leaf);
	else
		return -ENOENT;
	return 0;
}

static int mkdirs(const char *dir)
{
	char tmp[PATH_MAX];
	char *p;

	snprintf(tmp, sizeof(tmp), "%s", dir);
	for (p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(tmp, 0755) && errno != EEXIST)
			return -errno;
		*p = '/';
	}
	if (mkdir(tmp, 0755) && errno != EEXIST)
		return -errno;
	return 0;
}

static int write_atomic(const char *path, const char *text)
{
	char tmp[PATH_MAX + 8];
	FILE *f;
	int err;

	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	f = fopen(tmp, "w");
	if (!f)
		return -errno;
	fputs(text, f);
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

static int state_path(char *out, size_t len)
{
	return config_dir(out, len, "XDG_CONFIG_HOME", ".config", "perfect-sense/color.conf");
}

static void load_choice(struct ps_color_state *st)
{
	char path[PATH_MAX], line[PATH_MAX + 32];
	FILE *f;

	snprintf(st->mode, sizeof(st->mode), "native");
	snprintf(st->profile, sizeof(st->profile), "edid");
	if (state_path(path, sizeof(path)))
		return;
	f = fopen(path, "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\n")] = '\0';
		if (!strncmp(line, "mode=", 5) && (!strcmp(line + 5, "srgb") || !strcmp(line + 5, "native")))
			snprintf(st->mode, sizeof(st->mode), "%s", line + 5);
		else if (!strncmp(line, "profile=", 8) && line[8])
			snprintf(st->profile, sizeof(st->profile), "%s", line + 8);
	}
	fclose(f);
}

static int save_choice(const char *mode, const char *profile)
{
	char path[PATH_MAX], text[PATH_MAX + 128];
	char *slash;
	int err;

	err = state_path(path, sizeof(path));
	if (err)
		return err;
	slash = strrchr(path, '/');
	*slash = '\0';
	err = mkdirs(path);
	*slash = '/';
	if (err)
		return err;
	snprintf(text, sizeof(text), "# Perfect Sense panel color (perfectsense color …)\nmode=%s\nprofile=%s\n",
		 mode, profile);
	return write_atomic(path, text);
}

/* -------------------------------------------------------------- ICC out --- */

/* Matrix/shaper ICC from the EDID characterization, for compositors that want a file. */
int ps_color_write_edid_icc(const struct ps_color_panel *panel, char *out, size_t len)
{
	char dir[PATH_MAX], desc[96];
	cmsCIExyY white = { panel->white[0], panel->white[1], 1.0 };
	cmsCIExyYTRIPLE prim = { { panel->red[0], panel->red[1], 1.0 },
				 { panel->green[0], panel->green[1], 1.0 },
				 { panel->blue[0], panel->blue[1], 1.0 } };
	cmsToneCurve *curve;
	cmsToneCurve *curves[3];
	cmsHPROFILE profile;
	cmsMLU *mlu;
	int err, ok;

	err = config_dir(dir, sizeof(dir), "XDG_DATA_HOME", ".local/share", "perfect-sense");
	if (err)
		return err;
	err = mkdirs(dir);
	if (err)
		return err;
	snprintf(out, len, "%s/%s-%s%04x-edid.icc", dir, panel->connector, panel->vendor, panel->product);

	curve = cmsBuildGamma(NULL, panel->gamma);
	if (!curve)
		return -ENOMEM;
	curves[0] = curves[1] = curves[2] = curve;
	profile = cmsCreateRGBProfile(&white, &prim, curves);
	cmsFreeToneCurve(curve);
	if (!profile)
		return -EINVAL;
	snprintf(desc, sizeof(desc), "Perfect Sense EDID %s %s", panel->vendor,
		 panel->model[0] ? panel->model : "panel");
	mlu = cmsMLUalloc(NULL, 1);
	if (mlu) {
		cmsMLUsetASCII(mlu, "en", "US", desc);
		cmsWriteTag(profile, cmsSigProfileDescriptionTag, mlu);
		cmsMLUfree(mlu);
	}
	ok = cmsSaveProfileToFile(profile, out);
	cmsCloseProfile(profile);
	return ok ? 0 : -EIO;
}

/* ---------------------------------------------------------- compositors --- */

static int process_running(const char *comm)
{
	DIR *proc = opendir("/proc");
	struct dirent *p;
	int found = 0;

	if (!proc)
		return 0;
	while (!found && (p = readdir(proc))) {
		char path[300], name[64];
		FILE *f;

		if (p->d_name[0] < '0' || p->d_name[0] > '9')
			continue;
		snprintf(path, sizeof(path), "/proc/%s/comm", p->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		if (fgets(name, sizeof(name), f)) {
			name[strcspn(name, "\n")] = '\0';
			found = !strcmp(name, comm);
		}
		fclose(f);
	}
	closedir(proc);
	return found;
}

const char *ps_color_compositor(void)
{
	if (process_running("umbriel"))
		return "umbriel";
	if (process_running("sway"))
		return "sway";
	if (process_running("kwin_wayland"))
		return "kde";
	if (process_running("Hyprland"))
		return "hyprland";
	if (process_running("gnome-shell"))
		return "gnome";
	return "unknown";
}

/* fork+exec without a shell: arguments may hold user paths. Returns the exit status or -errno. */
static int run(char *const argv[])
{
	pid_t pid = fork();
	int status;

	if (pid < 0)
		return -errno;
	if (pid == 0) {
		int null = open("/dev/null", O_WRONLY);

		if (null >= 0) {
			dup2(null, 1);
			dup2(null, 2);
		}
		execvp(argv[0], argv);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
		return -errno;
	return WIFEXITED(status) ? WEXITSTATUS(status) : -ECHILD;
}

static int toml_escape(const char *in, char *out, size_t len)
{
	size_t o = 0;

	for (; *in; in++) {
		if (*in == '\n' || *in == '\r')
			return -EINVAL;
		if (o + 3 >= len)
			return -ENAMETOOLONG;
		if (*in == '"' || *in == '\\')
			out[o++] = '\\';
		out[o++] = *in;
	}
	out[o] = '\0';
	return 0;
}

static int umbriel_include(char *out, size_t len)
{
	return config_dir(out, len, "XDG_CONFIG_HOME", ".config", "umbriel/" UMBRIEL_INCLUDE);
}

/* Whether the main Umbriel config lists the include (it reads nothing it does not list). */
static int umbriel_include_listed(void)
{
	char path[PATH_MAX], line[1024];
	FILE *f;
	int listed = 0;

	if (config_dir(path, sizeof(path), "XDG_CONFIG_HOME", ".config", "umbriel/config.toml"))
		return 0;
	f = fopen(path, "r");
	if (!f)
		return 0;
	while (!listed && fgets(line, sizeof(line), f))
		listed = line[0] != '#' && strstr(line, UMBRIEL_INCLUDE) != NULL;
	fclose(f);
	return listed;
}

static int apply_umbriel(const struct ps_color_panel *panel, const struct ps_color_state *st)
{
	char path[PATH_MAX], profile[PATH_MAX * 2], text[PATH_MAX * 2 + 512];
	char *const reload[] = { "umbriel", "msg", "config-reload", NULL };
	int err;

	err = umbriel_include(path, sizeof(path));
	if (err)
		return err;
	err = toml_escape(st->profile, profile, sizeof(profile));
	if (err)
		return err;
	if (!strcmp(st->mode, "srgb"))
		snprintf(text, sizeof(text),
			 "# Managed by Perfect Sense (perfectsense color srgb|native). Do not edit.\n"
			 "[output.\"%s\"]\ncolor_profile = \"%s\"\nsdr_gamut = \"srgb\"\n",
			 panel->connector, profile);
	else
		snprintf(text, sizeof(text),
			 "# Managed by Perfect Sense (perfectsense color srgb|native). Do not edit.\n"
			 "[output.\"%s\"]\ncolor_profile = \"none\"\nsdr_gamut = \"native\"\n",
			 panel->connector);
	err = write_atomic(path, text);
	if (err)
		return err;
	/* The optional include is watched too; the explicit reload just makes it immediate. */
	(void)run(reload);
	return umbriel_include_listed() ? 0 : -ENOLINK;
}

static int apply_sway(const struct ps_color_panel *panel, const struct ps_color_state *st, const char *icc)
{
	char *const srgb[] = { "swaymsg", "output", (char *)panel->connector, "color_profile", "icc",
			       (char *)icc, NULL };
	char *const native[] = { "swaymsg", "output", (char *)panel->connector, "color_profile", "srgb",
				 NULL };

	return run(!strcmp(st->mode, "srgb") ? srgb : native) ? -EIO : 0;
}

static int apply_kde(const struct ps_color_panel *panel, const struct ps_color_state *st, const char *icc)
{
	char arg[PATH_MAX + 128];
	char *const argv[] = { "kscreen-doctor", arg, NULL };

	if (strcmp(st->mode, "srgb"))
		snprintf(arg, sizeof(arg), "output.%s.colorProfileSource.sRGB", panel->connector);
	else if (!strcmp(st->profile, "edid"))
		snprintf(arg, sizeof(arg), "output.%s.colorProfileSource.EDID", panel->connector);
	else
		snprintf(arg, sizeof(arg), "output.%s.iccprofile.%s", panel->connector, icc);
	return run(argv) ? -EIO : 0;
}

/* ---------------------------------------------------------------- public --- */

static int valid_profile(const char *profile)
{
	struct stat sb;

	if (!strcmp(profile, "edid"))
		return 0;
	if (profile[0] != '/')
		return -EINVAL;
	if (stat(profile, &sb))
		return -errno;
	return S_ISREG(sb.st_mode) && !access(profile, R_OK) ? 0 : -EACCES;
}

/* What the running Umbriel stages for the panel: 1 the sRGB mapping, 0 nothing, -1 unknown (an
 * Umbriel without `umbriel color`, which predates color_profile: it applies after a restart). */
static int umbriel_staged(const char *connector)
{
	char buf[16384], key[64];
	const char *at;
	size_t n;
	FILE *p = popen("umbriel color --json 2>/dev/null", "r");

	if (!p)
		return -1;
	n = fread(buf, 1, sizeof(buf) - 1, p);
	buf[n] = '\0';
	if (pclose(p) != 0)
		return -1;
	snprintf(key, sizeof(key), "\"name\":\"%s\"", connector);
	at = strstr(buf, key);
	if (!at || !(at = strstr(at, "\"sdr_gamut_staged\":")))
		return -1;
	return !strncmp(at + 19, "true", 4);
}

int ps_color_status(struct ps_color_state *out)
{
	struct ps_color_panel panel;

	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	load_choice(out);
	snprintf(out->compositor, sizeof(out->compositor), "%s", ps_color_compositor());
	out->umbriel_include = -1;
	out->applied = -1;
	if (!strcmp(out->compositor, "umbriel")) {
		int staged = ps_color_panel(&panel) ? -1 : umbriel_staged(panel.connector);

		out->umbriel_include = umbriel_include_listed();
		if (staged >= 0)
			out->applied = staged == !strcmp(out->mode, "srgb");
	}
	return 0;
}

int ps_color_apply(struct ps_color_state *st, char *icc, size_t icc_len)
{
	struct ps_color_panel panel;
	const char *source;
	int err;

	err = ps_color_panel(&panel);
	if (err)
		return err;
	if (!strcmp(st->profile, "edid")) {
		err = ps_color_write_edid_icc(&panel, icc, icc_len);
		if (err)
			return err;
	} else {
		snprintf(icc, icc_len, "%s", st->profile);
	}
	source = st->compositor[0] ? st->compositor : ps_color_compositor();
	if (!strcmp(source, "umbriel"))
		err = apply_umbriel(&panel, st);
	else if (!strcmp(source, "sway"))
		err = apply_sway(&panel, st, icc);
	else if (!strcmp(source, "kde"))
		err = apply_kde(&panel, st, icc);
	else
		err = -EOPNOTSUPP;
	return err;
}

int ps_color_set(const char *mode, const char *profile, struct ps_color_state *st, char *icc, size_t icc_len)
{
	int err;

	if (!mode || (strcmp(mode, "srgb") && strcmp(mode, "native")))
		return -EINVAL;
	ps_color_status(st);
	if (profile) {
		err = valid_profile(profile);
		if (err)
			return err;
		snprintf(st->profile, sizeof(st->profile), "%s", profile);
	}
	snprintf(st->mode, sizeof(st->mode), "%s", mode);
	err = save_choice(st->mode, st->profile);
	if (err)
		return err;
	return ps_color_apply(st, icc, icc_len);
}
