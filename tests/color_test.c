/* SPDX-License-Identifier: AGPL-3.0-or-later */
#define _GNU_SOURCE

/* EDID parsing, gamut coverage and the EDID ICC writer, against the PHN16-72 panel's EDID. */

#include "perfectsense.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Innolux N160GME-GTB (Acer Predator PHN16-72), base block only. */
static const unsigned char panel_edid[] = {
  0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x0d, 0xae, 0x2f, 0x16,
  0x00, 0x00, 0x00, 0x00, 0x23, 0x20, 0x01, 0x04, 0xa5, 0x22, 0x16, 0x78,
  0x03, 0x0f, 0x95, 0xae, 0x52, 0x43, 0xb0, 0x26, 0x0f, 0x50, 0x54, 0x00,
  0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
  0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x6b, 0x6e, 0x00, 0xa0, 0xa0, 0x40,
  0x84, 0x60, 0x30, 0x20, 0x6a, 0x00, 0x58, 0xd7, 0x10, 0x00, 0x00, 0x18,
  0x00, 0x00, 0x00, 0xfd, 0x0c, 0x3c, 0xf0, 0xa1, 0xa1, 0x72, 0x01, 0x0a,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xfe, 0x00, 0x43,
  0x4d, 0x4e, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x00, 0x00, 0x00, 0xfe, 0x00, 0x4e, 0x31, 0x36, 0x30, 0x47, 0x4d, 0x45,
  0x2d, 0x47, 0x54, 0x42, 0x0a, 0x20, 0x01, 0xed, 0x70, 0x20, 0x79, 0x02,
};

static int failures;

static void check(int ok, const char *what)
{
	if (!ok) {
		fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

static int near(double a, double b)
{
	return fabs(a - b) < 0.002;
}

int main(void)
{
	static const double srgb[3][2] = { { 0.640, 0.330 }, { 0.300, 0.600 }, { 0.150, 0.060 } };
	struct ps_color_panel p;
	unsigned char broken[128];
	char icc[4096], dir[] = "/tmp/ps-color-test-XXXXXX";
	FILE *f;

	check(ps_color_parse_edid(panel_edid, sizeof(panel_edid), &p) == 0, "parses the panel EDID");
	check(!strcmp(p.vendor, "CMN"), "vendor CMN");
	check(p.product == 0x162f, "product 0x162f");
	check(!strcmp(p.model, "N160GME-GTB"), "model from the last 0xFE text");
	check(near(p.red[0], 0.6796) && near(p.red[1], 0.3203), "red primary");
	check(near(p.green[0], 0.2646) && near(p.green[1], 0.6904), "green primary");
	check(near(p.blue[0], 0.1503) && near(p.blue[1], 0.0595), "blue primary");
	check(near(p.white[0], 0.3134) && near(p.white[1], 0.3291), "white point");
	check(near(p.gamma, 2.2), "gamma 2.2");
	check(p.coverage_srgb > 0.99, "covers sRGB");
	check(p.coverage_p3 > 0.97, "covers nearly all of P3");
	check(p.wide, "classified wide gamut");

	/* An sRGB panel covers sRGB fully and is not wide. */
	memcpy(&p.red, srgb[0], sizeof(p.red));
	memcpy(&p.green, srgb[1], sizeof(p.green));
	memcpy(&p.blue, srgb[2], sizeof(p.blue));
	check(near(ps_color_coverage(&p, srgb), 1.0), "sRGB covers itself");

	memset(broken, 0, sizeof(broken));
	check(ps_color_parse_edid(broken, sizeof(broken), &p) == -EINVAL, "rejects a non-EDID");
	check(ps_color_parse_edid(panel_edid, 64, &p) == -EINVAL, "rejects a short EDID");

	/* The ICC writer lands under XDG_DATA_HOME and produces an ICC ("acsp" at offset 36). */
	if (mkdtemp(dir)) {
		unsigned char head[40];

		setenv("XDG_DATA_HOME", dir, 1);
		ps_color_parse_edid(panel_edid, sizeof(panel_edid), &p);
		snprintf(p.connector, sizeof(p.connector), "eDP-1");
		check(ps_color_write_edid_icc(&p, icc, sizeof(icc)) == 0, "writes the EDID ICC");
		f = fopen(icc, "rb");
		check(f && fread(head, 1, sizeof(head), f) == sizeof(head) && !memcmp(head + 36, "acsp", 4),
		      "ICC signature");
		if (f)
			fclose(f);
		unlink(icc);
		snprintf(icc, sizeof(icc), "%s/perfect-sense", dir);
		rmdir(icc);
		rmdir(dir);
	}

	if (failures)
		return 1;
	puts("color: ok");
	return 0;
}
