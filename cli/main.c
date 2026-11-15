/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * perfectsense — one-shot CLI over libperfectsense (no daemon required).
 */

#define _POSIX_C_SOURCE 200809L

#include "perfectsense.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_json;

/* Ask perfectsensed to re-read fan-curve conf (SIGHUP; best-effort). */
static void daemon_reload(void)
{
	(void)system(
		"systemctl kill -s HUP perfectsensed.service >/dev/null 2>&1 || "
		"killall -HUP perfectsensed >/dev/null 2>&1");
}

static void usage(FILE *out)
{
	fputs(
		"Usage: perfectsense [--json] <command> [args]\n"
		"\n"
		"Commands:\n"
		"  status                      Full hardware status\n"
		"  temps                       CPU/GPU temps (°C)\n"
		"  mode get|set <name>         Thermal profile (+ nvidia-powerd)\n"
		"  gpu powerd get|on|off       Dynamic Boost service (~80W / ~140W)\n"
		"  gpu powermizer get|set …    Preferred Mode (best-effort)\n"
		"  gpu oc status|apply [mode]  Acer per-mode GPU clock offsets (dGPU must be awake)\n"
		"  hdmi status|on|off          HDMI/USB-C video via dGPU (off lets it sleep; applies at once)\n"
		"  fan get|set <cpu> <gpu>|auto\n"
		"  fan curve on|off|status|set|reload …\n"
		"  battery get|on|off          80% charge limiter\n"
		"  battery calibration get|on|off\n"
		"  usb get|set <0|10|20|30>    USB charging while off (minutes)\n"
		"  lcd get|set <0|1>           LCD override (bit-48 decode)\n"
		"  kb-timeout get|set <0|1>    KB backlight 30s timeout\n"
		"  boot-sound get|set <0|1>    Boot animation sound\n"
		"  rgb get|set <z1..z4> [bri]|off\n"
		"  rgb effect get|set <mode,speed,bri,dir,r,g,b>\n"
		"  logo get|set <RRGGBB> [bri] [en]|off\n"
		"  lights reset                Lighting controller reset\n"
		"  event inject <name>         Synthetic hotkey (turbo|mode|ac-*)\n"
		"  cpu status                  Governor/EPP/turbo/RAPL\n"
		"  cpu preset <name> [--sync-profile]\n"
		"  cpu restore                 Revert to baseline snapshot\n"
		"  cpu snapshot                Refresh baseline snapshot now\n"
		"  cpu set governor|epp|turbo|max-perf|pl1|pl2 …\n"
		"  help\n"
		"\n"
		"Mode names: low-power | quiet | balanced |\n"
		"            balanced-performance | performance\n"
		"Aliases:    eco→low-power, turbo→performance, bp→balanced-performance\n"
		"CPU presets: power|eco, balanced, performance|bp, turbo\n"
		"Fan curve: cpu='(90,100,95),(85,89,90)' gpu='…' interval_ms=1000\n"
		"  band = (temp_lo,temp_hi,duty%%) per axis (CPU and/or GPU)\n"
		"\n"
		"Env: PERFECTSENSE_SYSFS / PERFECTSENSE_KB_SYSFS / PERFECTSENSE_LOGO_SYSFS\n",
		out);
}

static int fail(int err)
{
	fprintf(stderr, "perfectsense: %s\n", ps_strerror(err));
	return 1;
}

static const char *json_int_or_null(int v, char *tmp, size_t n)
{
	if (v < 0) {
		snprintf(tmp, n, "null");
		return tmp;
	}
	snprintf(tmp, n, "%d", v);
	return tmp;
}

static int emit_status_json(const struct ps_status *s)
{
	char b1[16], b2[16], b3[16], b4[16], b5[16], b6[16];
	char r1[16], r2[16], t1[16], t2[16];
	char pd[16], pw[16], psync[16];

	printf(
		"{\"api_version\":%u,"
		"\"thermal_profile\":\"%s\","
		"\"thermal_profile_raw\":\"0x%02x\","
		"\"ac_online\":%s,"
		"\"platform_profile\":\"%s\","
		"\"fan\":{"
		"\"duty\":{\"cpu\":%u,\"gpu\":%u},"
		"\"target\":{\"cpu\":%u,\"gpu\":%u},"
		"\"auto\":%s,"
		"\"rpm\":{\"cpu\":%s,\"gpu\":%s}},"
		"\"temps\":{\"cpu\":%s,\"gpu\":%s},"
		"\"battery_limiter\":%s,"
		"\"battery_calibration\":%s,"
		"\"usb_charging\":%s,"
		"\"lcd_override\":%s,"
		"\"backlight_timeout\":%s,"
		"\"boot_animation_sound\":%s,"
		"\"rgb\":\"%s\","
		"\"rgb_effect\":\"%s\","
		"\"logo\":\"%s\","
		"\"nvidia_powerd\":%s,"
		"\"nvidia_powerd_wanted\":%s,"
		"\"nvidia_powerd_synced\":%s"
		"}\n",
		s->api_version, s->thermal_profile, s->thermal_profile_raw,
		s->ac_online ? "true" : "false", s->platform_profile,
		s->fan_duty_cpu, s->fan_duty_gpu, s->fan_target_cpu,
		s->fan_target_gpu, s->fan_auto ? "true" : "false",
		json_int_or_null(s->fan_rpm_cpu, r1, sizeof(r1)),
		json_int_or_null(s->fan_rpm_gpu, r2, sizeof(r2)),
		json_int_or_null(s->temp_cpu, t1, sizeof(t1)),
		json_int_or_null(s->temp_gpu, t2, sizeof(t2)),
		json_int_or_null(s->battery_limiter, b1, sizeof(b1)),
		json_int_or_null(s->battery_calibration, b2, sizeof(b2)),
		json_int_or_null(s->usb_charging, b3, sizeof(b3)),
		json_int_or_null(s->lcd_override, b4, sizeof(b4)),
		json_int_or_null(s->backlight_timeout, b5, sizeof(b5)),
		json_int_or_null(s->boot_animation_sound, b6, sizeof(b6)),
		s->rgb, s->rgb_effect, s->logo,
		json_int_or_null(s->nvidia_powerd, pd, sizeof(pd)),
		json_int_or_null(s->nvidia_powerd_wanted, pw, sizeof(pw)),
		json_int_or_null(s->nvidia_powerd_synced, psync, sizeof(psync)));
	return 0;
}

static int emit_status_text(const struct ps_status *s)
{
	printf("api_version: %u\n", s->api_version);
	printf("thermal_profile: %s (0x%02x)\n", s->thermal_profile,
	       s->thermal_profile_raw);
	printf("platform_profile: %s\n", s->platform_profile);
	printf("ac_online: %s\n", s->ac_online ? "yes" : "no");
	printf("fan_duty: %u,%u\n", s->fan_duty_cpu, s->fan_duty_gpu);
	printf("fan_target: %u,%u%s\n", s->fan_target_cpu, s->fan_target_gpu,
	       s->fan_auto ? " (auto)" : "");
	if (s->fan_rpm_cpu >= 0 || s->fan_rpm_gpu >= 0)
		printf("fan_rpm: %d,%d\n", s->fan_rpm_cpu, s->fan_rpm_gpu);
	if (s->temp_cpu >= 0 || s->temp_gpu >= 0)
		printf("temps_c: %d,%d\n", s->temp_cpu, s->temp_gpu);
	printf("battery_limiter: %d\n", s->battery_limiter);
	printf("battery_calibration: %d\n", s->battery_calibration);
	printf("usb_charging: %d\n", s->usb_charging);
	printf("lcd_override: %d\n", s->lcd_override);
	printf("backlight_timeout: %d\n", s->backlight_timeout);
	printf("boot_animation_sound: %d\n", s->boot_animation_sound);
	printf("rgb: %s\n", s->rgb[0] ? s->rgb : "(unavailable)");
	printf("rgb_effect: %s\n",
	       s->rgb_effect[0] ? s->rgb_effect : "(unavailable)");
	printf("logo: %s\n", s->logo[0] ? s->logo : "(unavailable)");
	if (s->nvidia_powerd >= 0) {
		printf("nvidia_powerd: %s (wanted %s, synced %s)\n",
		       s->nvidia_powerd ? "on" : "off",
		       s->nvidia_powerd_wanted < 0
			       ? "?"
			       : (s->nvidia_powerd_wanted ? "on" : "off"),
		       s->nvidia_powerd_synced < 0
			       ? "?"
			       : (s->nvidia_powerd_synced ? "yes" : "NO"));
		if (s->nvidia_powerd_synced == 0)
			fputs("warning: nvidia-powerd out of sync with thermal "
			      "profile (Dynamic Boost may be wrong)\n",
			      stderr);
	}
	return 0;
}

static int cmd_status(void)
{
	struct ps_status st;
	int err = ps_status_read(&st);

	if (err)
		return fail(err);
	return g_json ? emit_status_json(&st) : emit_status_text(&st);
}

static int cmd_mode(int argc, char **argv)
{
	const char *sub;
	char buf[64];
	int err;

	if (argc < 1) {
		usage(stderr);
		return 1;
	}
	sub = argv[0];

	if (!strcmp(sub, "get")) {
		err = ps_mode_get(buf, sizeof(buf));
		if (err)
			return fail(err);
		if (g_json)
			printf("{\"thermal_profile\":\"%s\"}\n", buf);
		else
			puts(buf);
		return 0;
	}

	if (!strcmp(sub, "set")) {
		if (argc < 2) {
			fputs("perfectsense: mode set requires <name>\n",
			      stderr);
			return 1;
		}
		err = ps_mode_set(argv[1]);
		if (err == -EBUSY) {
			/* Thermal applied; powerd sync failed — warn UIs/humans. */
			if (g_json)
				printf("{\"ok\":true,\"thermal_profile\":\"%s\","
				       "\"nvidia_powerd_synced\":false,"
				       "\"warning\":\"nvidia-powerd sync failed\"}\n",
				       argv[1]);
			else {
				printf("thermal_profile set to %s\n", argv[1]);
				fprintf(stderr,
					"perfectsense: warning: %s\n",
					ps_strerror(err));
			}
			return 2;
		}
		if (err)
			return fail(err);
		if (g_json)
			printf("{\"ok\":true,\"thermal_profile\":\"%s\","
			       "\"nvidia_powerd_synced\":true}\n",
			       argv[1]);
		else
			printf("thermal_profile set to %s\n", argv[1]);
		return 0;
	}

	fprintf(stderr, "perfectsense: unknown mode subcommand '%s'\n", sub);
	return 1;
}

static int cmd_fan(int argc, char **argv)
{
	const char *sub;
	unsigned cpu, gpu;
	int err;

	if (argc < 1) {
		usage(stderr);
		return 1;
	}
	sub = argv[0];

	if (!strcmp(sub, "curve")) {
		struct ps_fan_curve_config cfg;
		int cpu_c = 0, gpu_c = 0, i;
		unsigned eval_c = 0, eval_g = 0;

		if (argc < 2) {
			fputs("perfectsense: fan curve on|off|status|set|reload …\n",
			      stderr);
			return 1;
		}
		ps_fan_curve_config_load(&cfg);
		if (!strcmp(argv[1], "status")) {
			(void)ps_temps_read(&cpu_c, &gpu_c);
			ps_fan_curve_eval(&cfg, cpu_c, gpu_c, &eval_c, &eval_g);
			if (g_json) {
				printf("{\"enabled\":%s,\"interval_ms\":%d,",
				       cfg.enabled ? "true" : "false",
				       cfg.interval_ms);
				printf("\"temp_cpu\":%d,\"temp_gpu\":%d,"
				       "\"eval\":{\"cpu\":%u,\"gpu\":%u},\"cpu\":[",
				       cpu_c, gpu_c, eval_c, eval_g);
				for (i = 0; i < cfg.cpu.n_bands; i++)
					printf("%s{\"lo\":%d,\"hi\":%d,\"duty\":%u}",
					       i ? "," : "",
					       cfg.cpu.bands[i].temp_lo,
					       cfg.cpu.bands[i].temp_hi,
					       cfg.cpu.bands[i].duty_pct);
				printf("],\"gpu\":[");
				for (i = 0; i < cfg.gpu.n_bands; i++)
					printf("%s{\"lo\":%d,\"hi\":%d,\"duty\":%u}",
					       i ? "," : "",
					       cfg.gpu.bands[i].temp_lo,
					       cfg.gpu.bands[i].temp_hi,
					       cfg.gpu.bands[i].duty_pct);
				puts("]}");
			} else {
				printf("fan curve: %s\n",
				       cfg.enabled ? "on" : "off");
				printf("interval_ms: %d\n", cfg.interval_ms);
				printf("cpu bands (lo,hi,duty%%):");
				for (i = 0; i < cfg.cpu.n_bands; i++)
					printf(" (%d,%d,%u)",
					       cfg.cpu.bands[i].temp_lo,
					       cfg.cpu.bands[i].temp_hi,
					       cfg.cpu.bands[i].duty_pct);
				putchar('\n');
				printf("gpu bands (lo,hi,duty%%):");
				for (i = 0; i < cfg.gpu.n_bands; i++)
					printf(" (%d,%d,%u)",
					       cfg.gpu.bands[i].temp_lo,
					       cfg.gpu.bands[i].temp_hi,
					       cfg.gpu.bands[i].duty_pct);
				putchar('\n');
				printf("temp: cpu=%d gpu=%d → eval duty %u,%u\n",
				       cpu_c, gpu_c, eval_c, eval_g);
				puts("note: timer runs in perfectsensed when enabled");
			}
			return 0;
		}
		if (!strcmp(argv[1], "set")) {
			for (i = 2; i < argc; i++) {
				char *eq = strchr(argv[i], '=');
				char key[32];

				if (!eq) {
					fputs("perfectsense: expected key=value\n",
					      stderr);
					return 1;
				}
				*eq = '\0';
				snprintf(key, sizeof(key), "%s", argv[i]);
				if (!strcmp(key, "interval_ms")) {
					cfg.interval_ms = atoi(eq + 1);
				} else if (!strcmp(key, "enabled")) {
					cfg.enabled = atoi(eq + 1) ? 1 : 0;
				} else if (!strcmp(key, "cpu")) {
					err = ps_fan_curve_parse_bands(eq + 1,
								       &cfg.cpu);
					if (err)
						return fail(err);
				} else if (!strcmp(key, "gpu")) {
					err = ps_fan_curve_parse_bands(eq + 1,
								       &cfg.gpu);
					if (err)
						return fail(err);
				} else {
					fprintf(stderr,
						"perfectsense: unknown key '%s' (use cpu= gpu= interval_ms=)\n",
						key);
					return 1;
				}
				*eq = '=';
			}
			err = ps_fan_curve_config_save(&cfg);
			if (err)
				return fail(err);
			daemon_reload();
			if (g_json)
				puts("{\"ok\":true}");
			else
				puts("fan curve config saved (daemon reloaded)");
			return 0;
		}
		if (!strcmp(argv[1], "on") || !strcmp(argv[1], "off")) {
			cfg.enabled = !strcmp(argv[1], "on");
			err = ps_fan_curve_config_save(&cfg);
			if (err)
				return fail(err);
			daemon_reload();
			if (!cfg.enabled)
				(void)ps_fan_set_auto();
			if (g_json)
				printf("{\"ok\":true,\"fan_curve\":%s}\n",
				       cfg.enabled ? "true" : "false");
			else
				printf("fan curve: %s\n",
				       cfg.enabled ? "on" : "off");
			return 0;
		}
		if (!strcmp(argv[1], "reload")) {
			daemon_reload();
			if (g_json)
				puts("{\"ok\":true}");
			else
				puts("fan curve: daemon reload requested");
			return 0;
		}
		fputs("perfectsense: fan curve on|off|status|set|reload …\n",
		      stderr);
		return 1;
	}

	if (!strcmp(sub, "get")) {
		err = ps_fan_duty_get(&cpu, &gpu);
		if (err)
			return fail(err);
		{
			unsigned tc, tg;
			int auto_mode;
			int rc = -1, rg = -1;

			err = ps_fan_target_get(&tc, &tg);
			if (err)
				return fail(err);
			auto_mode = (tc == 0 && tg == 0);
			(void)ps_fan_rpm_get(&rc, &rg);
			if (g_json) {
				printf(
					"{\"duty\":{\"cpu\":%u,\"gpu\":%u},"
					"\"target\":{\"cpu\":%u,\"gpu\":%u},"
					"\"auto\":%s,"
					"\"rpm\":{\"cpu\":%d,\"gpu\":%d}}\n",
					cpu, gpu, tc, tg,
					auto_mode ? "true" : "false", rc, rg);
			} else {
				printf("duty: %u,%u\n", cpu, gpu);
				printf("target: %u,%u%s\n", tc, tg,
				       auto_mode ? " (auto)" : "");
				if (rc >= 0 || rg >= 0)
					printf("rpm: %d,%d\n", rc, rg);
			}
		}
		return 0;
	}

	if (!strcmp(sub, "auto")) {
		err = ps_fan_set_auto();
		if (err)
			return fail(err);
		if (g_json)
			puts("{\"ok\":true,\"target\":{\"cpu\":0,\"gpu\":0},"
			     "\"auto\":true}");
		else
			puts("fan target: 0,0 (auto)");
		return 0;
	}

	if (!strcmp(sub, "set")) {
		unsigned long c, g;
		char *end;

		if (argc < 3) {
			fputs("perfectsense: fan set requires <cpu> <gpu>\n",
			      stderr);
			return 1;
		}
		c = strtoul(argv[1], &end, 10);
		if (*end || c > 100) {
			fputs("perfectsense: cpu percent must be 0-100\n",
			      stderr);
			return 1;
		}
		g = strtoul(argv[2], &end, 10);
		if (*end || g > 100) {
			fputs("perfectsense: gpu percent must be 0-100\n",
			      stderr);
			return 1;
		}
		err = ps_fan_set((unsigned)c, (unsigned)g);
		if (err)
			return fail(err);
		if (g_json)
			printf("{\"ok\":true,\"target\":{\"cpu\":%lu,\"gpu\":%lu},"
			       "\"auto\":%s}\n",
			       c, g, (c == 0 && g == 0) ? "true" : "false");
		else
			printf("fan target: %lu,%lu\n", c, g);
		return 0;
	}

	fprintf(stderr, "perfectsense: unknown fan subcommand '%s'\n", sub);
	return 1;
}

static int emit_attr(const char *name, int value)
{
	if (g_json)
		printf("{\"%s\":%d}\n", name, value);
	else
		printf("%d\n", value);
	return 0;
}

static int cmd_bool_attr(const char *sysfs_name, int argc, char **argv)
{
	const char *sub;
	int value;
	int err;

	if (argc < 1) {
		usage(stderr);
		return 1;
	}
	sub = argv[0];

	if (!strcmp(sub, "get")) {
		err = ps_attr_get_int(sysfs_name, &value);
		if (err)
			return fail(err);
		return emit_attr(sysfs_name, value);
	}
	if (!strcmp(sub, "on") || !strcmp(sub, "off")) {
		err = ps_attr_set_int(sysfs_name, !strcmp(sub, "on") ? 1 : 0);
		if (err)
			return fail(err);
		if (g_json)
			printf("{\"ok\":true,\"%s\":%d}\n", sysfs_name,
			       !strcmp(sub, "on") ? 1 : 0);
		else
			printf("%s: %s\n", sysfs_name, sub);
		return 0;
	}
	if (!strcmp(sub, "set")) {
		long v;
		char *end;

		if (argc < 2) {
			fprintf(stderr, "perfectsense: set requires <value>\n");
			return 1;
		}
		v = strtol(argv[1], &end, 10);
		if (*end) {
			fputs("perfectsense: invalid value\n", stderr);
			return 1;
		}
		err = ps_attr_set_int(sysfs_name, (int)v);
		if (err)
			return fail(err);
		if (g_json)
			printf("{\"ok\":true,\"%s\":%ld}\n", sysfs_name, v);
		else
			printf("%s: %ld\n", sysfs_name, v);
		return 0;
	}

	fprintf(stderr, "perfectsense: unknown subcommand '%s'\n", sub);
	return 1;
}

static int cmd_battery(int argc, char **argv)
{
	if (argc >= 1 && !strcmp(argv[0], "calibration"))
		return cmd_bool_attr("battery_calibration", argc - 1, argv + 1);
	return cmd_bool_attr("battery_limiter", argc, argv);
}

static int cmd_rgb(int argc, char **argv)
{
	char buf[64];
	int err;

	if (argc < 1) {
		usage(stderr);
		return 1;
	}
	if (!strcmp(argv[0], "effect")) {
		if (argc < 2) {
			fputs("perfectsense: rgb effect get|set …\n", stderr);
			return 1;
		}
		if (!strcmp(argv[1], "get")) {
			err = ps_rgb_effect_get(buf, sizeof(buf));
			if (err)
				return fail(err);
			if (g_json)
				printf("{\"rgb_effect\":\"%s\"}\n", buf);
			else
				puts(buf);
			return 0;
		}
		if (!strcmp(argv[1], "set")) {
			if (argc < 3) {
				fputs("perfectsense: rgb effect set <csv>\n",
				      stderr);
				return 1;
			}
			err = ps_rgb_effect_set(argv[2]);
			if (err)
				return fail(err);
			if (g_json)
				printf("{\"ok\":true,\"rgb_effect\":\"%s\"}\n",
				       argv[2]);
			else
				puts(argv[2]);
			return 0;
		}
		fputs("perfectsense: rgb effect get|set …\n", stderr);
		return 1;
	}
	if (!strcmp(argv[0], "get")) {
		err = ps_rgb_get(buf, sizeof(buf));
		if (err)
			return fail(err);
		if (g_json)
			printf("{\"rgb\":\"%s\"}\n", buf);
		else
			puts(buf);
		return 0;
	}
	if (!strcmp(argv[0], "off")) {
		err = ps_rgb_off();
		if (err)
			return fail(err);
		if (g_json)
			puts("{\"ok\":true,\"rgb\":\"000000,000000,000000,000000,0\"}");
		else
			puts("rgb off");
		return 0;
	}
	if (!strcmp(argv[0], "set")) {
		char csv[64];
		const char *bri = "60";

		if (argc < 5) {
			fputs("perfectsense: rgb set needs z1 z2 z3 z4 [bri]\n",
			      stderr);
			return 1;
		}
		if (argc >= 6)
			bri = argv[5];
		snprintf(csv, sizeof(csv), "%s,%s,%s,%s,%s", argv[1], argv[2],
			 argv[3], argv[4], bri);
		err = ps_rgb_set(csv);
		if (err)
			return fail(err);
		if (g_json)
			printf("{\"ok\":true,\"rgb\":\"%s\"}\n", csv);
		else
			puts(csv);
		return 0;
	}
	fprintf(stderr, "perfectsense: unknown rgb subcommand '%s'\n", argv[0]);
	return 1;
}

static int cmd_logo(int argc, char **argv)
{
	char buf[64];
	int err;

	if (argc < 1) {
		usage(stderr);
		return 1;
	}
	if (!strcmp(argv[0], "get")) {
		err = ps_logo_get(buf, sizeof(buf));
		if (err)
			return fail(err);
		if (g_json)
			printf("{\"logo\":\"%s\"}\n", buf);
		else
			puts(buf);
		return 0;
	}
	if (!strcmp(argv[0], "off")) {
		err = ps_logo_off();
		if (err)
			return fail(err);
		if (g_json)
			puts("{\"ok\":true,\"logo\":\"000000,0,0\"}");
		else
			puts("logo off");
		return 0;
	}
	if (!strcmp(argv[0], "set")) {
		char csv[40];
		const char *bri = "70";
		const char *en = "1";

		if (argc < 2) {
			fputs("perfectsense: logo set needs <RRGGBB> [bri] [en]\n",
			      stderr);
			return 1;
		}
		if (argc >= 3)
			bri = argv[2];
		if (argc >= 4)
			en = argv[3];
		snprintf(csv, sizeof(csv), "%s,%s,%s", argv[1], bri, en);
		err = ps_logo_set(csv);
		if (err)
			return fail(err);
		if (g_json)
			printf("{\"ok\":true,\"logo\":\"%s\"}\n", csv);
		else
			puts(csv);
		return 0;
	}
	fprintf(stderr, "perfectsense: unknown logo subcommand '%s'\n", argv[0]);
	return 1;
}

static int cmd_gpu(int argc, char **argv)
{
	int mode, on, err;

	if (argc < 1) {
		fputs("perfectsense: gpu powerd|powermizer …\n", stderr);
		return 1;
	}

	if (!strcmp(argv[0], "powerd")) {
		if (argc < 2) {
			fputs("perfectsense: gpu powerd get|on|off\n", stderr);
			return 1;
		}
		if (!strcmp(argv[1], "get")) {
			err = ps_gpu_powerd_get(&on);
			if (err)
				return fail(err);
			if (g_json)
				printf("{\"nvidia_powerd\":%s}\n",
				       on ? "true" : "false");
			else
				printf("nvidia-powerd: %s\n",
				       on ? "on" : "off");
			return 0;
		}
		if (!strcmp(argv[1], "on") || !strcmp(argv[1], "off")) {
			err = ps_gpu_powerd_set(!strcmp(argv[1], "on"));
			if (err)
				return fail(err);
			if (g_json)
				printf("{\"ok\":true,\"nvidia_powerd\":%s}\n",
				       !strcmp(argv[1], "on") ? "true" :
								"false");
			else
				printf("nvidia-powerd: %s\n", argv[1]);
			return 0;
		}
		fputs("perfectsense: gpu powerd get|on|off\n", stderr);
		return 1;
	}

	if (!strcmp(argv[0], "oc")) {
		struct ps_gpu_oc_state st;
		char cur[32] = "";
		const char *which = argc >= 3 ? argv[2] : NULL;

		if (argc < 2) {
			fputs("perfectsense: gpu oc status|apply [mode]\n", stderr);
			return 1;
		}
		if (!which) {
			(void)ps_mode_get(cur, sizeof(cur));
			which = cur;
		}
		if (!strcmp(argv[1], "status")) {
			err = ps_gpu_oc_status(which, &st);
			if (err == -ENODEV) {
				if (g_json)
					fputs("{\"supported\":false}\n", stdout);
				else
					puts("gpu oc: board not covered");
				return 0;
			}
			if (err)
				return fail(err);
			if (g_json) {
				char live_core[16] = "null", live_mem[16] = "null";

				if (st.core_mhz != PS_GPU_OC_UNKNOWN) {
					snprintf(live_core, sizeof(live_core), "%d", st.core_mhz);
					snprintf(live_mem, sizeof(live_mem), "%d", st.mem_mhz);
				}
				printf("{\"supported\":true,\"awake\":%s,\"mode\":\"%s\","
				       "\"wanted\":{\"core\":%d,\"mem\":%d},"
				       "\"live\":{\"core\":%s,\"mem\":%s}}\n",
				       st.awake ? "true" : "false", which,
				       st.wanted_core_mhz, st.wanted_mem_mhz,
				       live_core, live_mem);
			} else {
				printf("mode %s wants +%d/+%d MHz; dGPU %s",
				       which, st.wanted_core_mhz, st.wanted_mem_mhz,
				       st.awake ? "awake" : "asleep (not touched)");
				if (st.core_mhz != PS_GPU_OC_UNKNOWN)
					printf("; live %+d/%+d MHz", st.core_mhz, st.mem_mhz);
				putchar('\n');
			}
			return 0;
		}
		if (!strcmp(argv[1], "apply")) {
			err = ps_gpu_oc_apply(which);
			if (err == -EAGAIN) {
				if (g_json)
					fputs("{\"ok\":false,\"pending\":true}\n", stdout);
				else
					puts("dGPU asleep: profile pending (not woken)");
				return 0;
			}
			if (err)
				return fail(err);
			if (g_json)
				fputs("{\"ok\":true}\n", stdout);
			else
				puts("gpu oc applied");
			return 0;
		}
		fputs("perfectsense: gpu oc status|apply [mode]\n", stderr);
		return 1;
	}

	if (!strcmp(argv[0], "powermizer")) {
		if (argc < 2) {
			fputs("perfectsense: gpu powermizer get|set …\n",
			      stderr);
			return 1;
		}
		if (!strcmp(argv[1], "get")) {
			err = ps_gpu_powermizer_get(&mode);
			if (err)
				return fail(err);
			if (g_json)
				printf("{\"powermizer\":\"%s\",\"value\":%d}\n",
				       ps_gpu_powermizer_name(mode), mode);
			else
				printf("%s (%d)\n",
				       ps_gpu_powermizer_name(mode), mode);
			return 0;
		}
		if (!strcmp(argv[1], "set")) {
			if (argc < 3) {
				fputs("perfectsense: gpu powermizer set <normal|auto|max>\n",
				      stderr);
				return 1;
			}
			err = ps_gpu_powermizer_from_name(argv[2], &mode);
			if (err)
				return fail(err);
			err = ps_gpu_powermizer_set(mode);
			if (err)
				return fail(err);
			if (g_json)
				printf("{\"ok\":true,\"powermizer\":\"%s\"}\n",
				       ps_gpu_powermizer_name(mode));
			else
				printf("powermizer: %s\n",
				       ps_gpu_powermizer_name(mode));
			return 0;
		}
		fputs("perfectsense: gpu powermizer get|set …\n", stderr);
		return 1;
	}

	fputs("perfectsense: gpu powerd|powermizer …\n", stderr);
	return 1;
}

static int cmd_hdmi(int argc, char **argv)
{
	struct ps_hdmi_state st;
	int err;

	if (argc >= 1 && (!strcmp(argv[0], "on") || !strcmp(argv[0], "off"))) {
		err = ps_hdmi_set(!strcmp(argv[0], "on"));
		if (err)
			return fail(err);
		/* Umbriel opens or releases the dGPU while reloading (older builds: next session). */
		(void)system("umbriel msg config-reload >/dev/null 2>&1");
		/* Releasing closes the device a moment after the reload returns. */
		nanosleep(&(struct timespec){ .tv_nsec = 500000000L }, NULL);
	} else if (argc >= 1 && strcmp(argv[0], "status") && strcmp(argv[0], "get")) {
		fputs("perfectsense: hdmi status|on|off\n", stderr);
		return 1;
	}
	err = ps_hdmi_status(&st);
	if (err)
		return fail(err);
	if (g_json) {
		printf("{\"hdmi\":\"%s\",\"session\":\"%s\",\"pending\":%s,"
		       "\"dgpu\":\"%s\",\"pci\":\"%s\"}\n",
		       st.configured ? "on" : "off",
		       st.session < 0 ? "none" : st.session ? "on" : "off",
		       st.session >= 0 && st.session != st.configured ? "true" : "false",
		       st.dgpu_awake < 0 ? "unknown" : st.dgpu_awake ? "active" : "suspended",
		       st.pci);
		return 0;
	}
	printf("hdmi: %s\n", st.configured ? "on" : "off");
	if (st.session >= 0)
		printf("session: %s%s\n", st.session ? "on" : "off",
		       st.session != st.configured ? " (restart the session to apply)" : "");
	printf("dgpu: %s\n", st.dgpu_awake < 0 ? "unknown" :
	       st.dgpu_awake ? "active" : "suspended");
	return 0;
}

static int cmd_temps(void)
{
	int cpu_c = 0, gpu_c = 0;
	int err = ps_temps_read(&cpu_c, &gpu_c);

	if (err)
		return fail(err);
	if (g_json)
		printf("{\"temp_cpu\":%d,\"temp_gpu\":%d}\n", cpu_c, gpu_c);
	else
		printf("temp_c: %d,%d\n", cpu_c, gpu_c);
	return 0;
}

static int cmd_event(int argc, char **argv)
{
	int err;

	if (argc < 2 || strcmp(argv[0], "inject") != 0) {
		fputs("perfectsense: event inject <turbo|mode|ac-online|ac-offline>\n",
		      stderr);
		return 1;
	}
	err = ps_event_inject(argv[1]);
	if (err)
		return fail(err);
	if (g_json)
		printf("{\"ok\":true,\"event\":\"%s\"}\n", argv[1]);
	else
		printf("injected event: %s\n", argv[1]);
	return 0;
}

static int cmd_lights(int argc, char **argv)
{
	int err;

	if (argc < 1 || strcmp(argv[0], "reset")) {
		fputs("perfectsense: usage: lights reset\n", stderr);
		return 1;
	}
	err = ps_lighting_reset();
	if (err)
		return fail(err);
	if (g_json)
		puts("{\"ok\":true,\"lights\":\"reset\"}");
	else
		puts("lighting reset");
	return 0;
}

static int emit_cpu_json(const struct ps_cpu_power *c)
{
	printf(
		"{\"available\":%s,"
		"\"governor\":\"%s\","
		"\"epp\":\"%s\","
		"\"turbo\":%s,"
		"\"no_turbo\":%d,"
		"\"max_perf_pct\":%u,"
		"\"min_perf_pct\":%u,"
		"\"scaling_max_khz\":%u,"
		"\"scaling_min_khz\":%u,"
		"\"cpuinfo_max_khz\":%u,"
		"\"pl1_uw\":%lu,"
		"\"pl2_uw\":%lu,"
		"\"pl1_w\":%.3f,"
		"\"pl2_w\":%.3f}\n",
		c->available ? "true" : "false", c->governor, c->epp,
		c->no_turbo == 0 ? "true" :
		c->no_turbo == 1 ? "false" : "null",
		c->no_turbo, c->max_perf_pct, c->min_perf_pct,
		c->scaling_max_khz, c->scaling_min_khz, c->cpuinfo_max_khz,
		c->pl1_uw, c->pl2_uw, c->pl1_uw / 1000000.0,
		c->pl2_uw / 1000000.0);
	return 0;
}

static int emit_cpu_text(const struct ps_cpu_power *c)
{
	printf("governor: %s\n", c->governor);
	printf("epp: %s\n", c->epp[0] ? c->epp : "(n/a)");
	if (c->no_turbo < 0)
		printf("turbo: n/a\n");
	else
		printf("turbo: %s (no_turbo=%d)\n",
		       c->no_turbo ? "off" : "on", c->no_turbo);
	printf("max_perf_pct: %u\n", c->max_perf_pct);
	printf("min_perf_pct: %u\n", c->min_perf_pct);
	printf("scaling_max_khz: %u\n", c->scaling_max_khz);
	printf("scaling_min_khz: %u\n", c->scaling_min_khz);
	printf("pl1_w: %.3f (%lu uW)\n", c->pl1_uw / 1000000.0, c->pl1_uw);
	printf("pl2_w: %.3f (%lu uW)\n", c->pl2_uw / 1000000.0, c->pl2_uw);
	return 0;
}

static int cmd_cpu(int argc, char **argv)
{
	struct ps_cpu_power st;
	int err;

	if (argc < 1) {
		usage(stderr);
		return 1;
	}

	if (!strcmp(argv[0], "status") || !strcmp(argv[0], "get")) {
		err = ps_cpu_power_read(&st);
		if (err)
			return fail(err);
		return g_json ? emit_cpu_json(&st) : emit_cpu_text(&st);
	}

	if (!strcmp(argv[0], "restore")) {
		err = ps_cpu_power_restore();
		if (err)
			return fail(err);
		if (g_json)
			puts("{\"ok\":true,\"cpu\":\"restored\"}");
		else
			puts("cpu power restored from snapshot");
		return 0;
	}

	if (!strcmp(argv[0], "snapshot")) {
		err = ps_cpu_power_snapshot_save();
		if (err)
			return fail(err);
		if (g_json)
			puts("{\"ok\":true,\"cpu\":\"snapshot\"}");
		else
			puts("cpu baseline snapshot saved");
		return 0;
	}

	if (!strcmp(argv[0], "preset")) {
		int sync = 0;
		int i;

		if (argc < 2) {
			fputs("perfectsense: cpu preset <name> [--sync-profile]\n",
			      stderr);
			return 1;
		}
		for (i = 2; i < argc; i++) {
			if (!strcmp(argv[i], "--sync-profile"))
				sync = 1;
			else {
				fprintf(stderr,
					"perfectsense: unknown flag '%s'\n",
					argv[i]);
				return 1;
			}
		}
		err = ps_cpu_power_preset(argv[1], sync);
		if (err)
			return fail(err);
		if (g_json)
			printf("{\"ok\":true,\"preset\":\"%s\",\"sync_profile\":%s}\n",
			       argv[1], sync ? "true" : "false");
		else
			printf("cpu preset: %s%s\n", argv[1],
			       sync ? " (+ platform profile)" : "");
		return 0;
	}

	if (!strcmp(argv[0], "set")) {
		if (argc < 3) {
			fputs("perfectsense: cpu set <governor|epp|turbo|max-perf|pl1|pl2> <value>\n",
			      stderr);
			return 1;
		}
		if (!strcmp(argv[1], "governor")) {
			err = ps_cpu_power_set_governor(argv[2]);
		} else if (!strcmp(argv[1], "epp")) {
			err = ps_cpu_power_set_epp(argv[2]);
		} else if (!strcmp(argv[1], "turbo")) {
			int on;
			if (!strcmp(argv[2], "on") || !strcmp(argv[2], "1"))
				on = 1;
			else if (!strcmp(argv[2], "off") ||
				 !strcmp(argv[2], "0"))
				on = 0;
			else {
				fputs("perfectsense: turbo on|off\n", stderr);
				return 1;
			}
			err = ps_cpu_power_set_turbo(on);
		} else if (!strcmp(argv[1], "max-perf")) {
			unsigned long pct = strtoul(argv[2], NULL, 10);
			err = ps_cpu_power_set_max_perf_pct((unsigned)pct);
		} else if (!strcmp(argv[1], "pl1") || !strcmp(argv[1], "pl2")) {
			double watts = strtod(argv[2], NULL);
			unsigned long uw;

			if (watts <= 0) {
				fputs("perfectsense: watts must be > 0\n",
				      stderr);
				return 1;
			}
			uw = (unsigned long)(watts * 1000000.0 + 0.5);
			err = !strcmp(argv[1], "pl1") ?
				      ps_cpu_power_set_pl1_uw(uw) :
				      ps_cpu_power_set_pl2_uw(uw);
		} else {
			fprintf(stderr, "perfectsense: unknown cpu set '%s'\n",
				argv[1]);
			return 1;
		}
		if (err)
			return fail(err);
		if (g_json)
			printf("{\"ok\":true,\"%s\":\"%s\"}\n", argv[1],
			       argv[2]);
		else
			printf("cpu %s: %s\n", argv[1], argv[2]);
		return 0;
	}

	fprintf(stderr, "perfectsense: unknown cpu subcommand '%s'\n", argv[0]);
	return 1;
}

int main(int argc, char **argv)
{
	int i = 1;
	const char *cmd;

	while (i < argc && argv[i][0] == '-') {
		if (!strcmp(argv[i], "--json")) {
			g_json = 1;
			i++;
		} else if (!strcmp(argv[i], "--help") ||
			   !strcmp(argv[i], "-h")) {
			usage(stdout);
			return 0;
		} else if (!strcmp(argv[i], "--")) {
			i++;
			break;
		} else {
			fprintf(stderr, "perfectsense: unknown option '%s'\n",
				argv[i]);
			usage(stderr);
			return 1;
		}
	}

	if (i >= argc) {
		usage(stderr);
		return 1;
	}

	cmd = argv[i++];

	if (!strcmp(cmd, "help") || !strcmp(cmd, "--help")) {
		usage(stdout);
		return 0;
	}
	if (!strcmp(cmd, "status"))
		return cmd_status();
	if (!strcmp(cmd, "temps"))
		return cmd_temps();
	if (!strcmp(cmd, "gpu"))
		return cmd_gpu(argc - i, argv + i);
	if (!strcmp(cmd, "hdmi"))
		return cmd_hdmi(argc - i, argv + i);
	if (!strcmp(cmd, "mode"))
		return cmd_mode(argc - i, argv + i);
	if (!strcmp(cmd, "fan"))
		return cmd_fan(argc - i, argv + i);
	if (!strcmp(cmd, "battery"))
		return cmd_battery(argc - i, argv + i);
	if (!strcmp(cmd, "usb"))
		return cmd_bool_attr("usb_charging", argc - i, argv + i);
	if (!strcmp(cmd, "lcd"))
		return cmd_bool_attr("lcd_override", argc - i, argv + i);
	if (!strcmp(cmd, "kb-timeout"))
		return cmd_bool_attr("backlight_timeout", argc - i, argv + i);
	if (!strcmp(cmd, "boot-sound"))
		return cmd_bool_attr("boot_animation_sound", argc - i, argv + i);
	if (!strcmp(cmd, "rgb"))
		return cmd_rgb(argc - i, argv + i);
	if (!strcmp(cmd, "logo"))
		return cmd_logo(argc - i, argv + i);
	if (!strcmp(cmd, "lights"))
		return cmd_lights(argc - i, argv + i);
	if (!strcmp(cmd, "event"))
		return cmd_event(argc - i, argv + i);
	if (!strcmp(cmd, "cpu"))
		return cmd_cpu(argc - i, argv + i);

	fprintf(stderr, "perfectsense: unknown command '%s'\n", cmd);
	usage(stderr);
	return 1;
}
