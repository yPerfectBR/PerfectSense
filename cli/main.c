/* SPDX-License-Identifier: AGPL-3.0-or-later */
/*
 * perfectsense — one-shot CLI over libperfectsense (no daemon required).
 */

#include "perfectsense.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_json;

/* Ask perfectsensed to re-read FanAggressor conf (best-effort). */
static void daemon_fa_reload(void)
{
	(void)system(
		"busctl --system call org.perfectsense.Sense1 "
		"/org/perfectsense/Sense1 org.perfectsense.Sense1 "
		"FanAggressorReload >/dev/null 2>&1");
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
		"  fan get|set <cpu> <gpu>|auto\n"
		"  fan aggressor on|off|status|set|reload key=val…\n"
		"  battery get|on|off          80% charge limiter\n"
		"  battery calibration get|on|off\n"
		"  usb get|set <0|10|20|30>    USB charging while off (minutes)\n"
		"  lcd get|set <0|1>           LCD override (may be -1 unsupported)\n"
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
		"\"logo\":\"%s\""
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
		s->rgb, s->rgb_effect, s->logo);
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
		if (err)
			return fail(err);
		if (g_json)
			printf("{\"ok\":true,\"thermal_profile\":\"%s\"}\n",
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

	if (!strcmp(sub, "aggressor")) {
		struct ps_fan_aggressor_config cfg;
		int cpu_c = 0, gpu_c = 0;

		if (argc < 2) {
			fputs("perfectsense: fan aggressor on|off|status|set|reload …\n",
			      stderr);
			return 1;
		}
		ps_fan_aggressor_config_load(&cfg);
		if (!strcmp(argv[1], "status")) {
			(void)ps_temps_read(&cpu_c, &gpu_c);
			if (g_json) {
				printf(
					"{\"enabled\":%s,\"engage_c\":%d,"
					"\"hysteresis_c\":%d,\"hold_sec\":%d,"
					"\"interval_ms\":%d,"
					"\"offset_cpu\":%d,\"offset_gpu\":%d,"
					"\"floor_cpu\":%d,\"floor_gpu\":%d,"
					"\"temp_cpu\":%d,\"temp_gpu\":%d}\n",
					cfg.enabled ? "true" : "false",
					cfg.engage_c, cfg.hysteresis_c,
					cfg.hold_sec, cfg.interval_ms,
					cfg.offset_cpu, cfg.offset_gpu,
					cfg.floor_cpu, cfg.floor_gpu, cpu_c,
					gpu_c);
			} else {
				printf("fan aggressor: %s\n",
				       cfg.enabled ? "on" : "off");
				printf("engage_c: %d (hyst %d, hold %ds, interval %dms)\n",
				       cfg.engage_c, cfg.hysteresis_c,
				       cfg.hold_sec, cfg.interval_ms);
				printf("offset: %d,%d  floor: %d,%d\n",
				       cfg.offset_cpu, cfg.offset_gpu,
				       cfg.floor_cpu, cfg.floor_gpu);
				printf("temp: cpu=%d gpu=%d\n", cpu_c, gpu_c);
				puts("note: timer runs in perfectsensed when enabled");
			}
			return 0;
		}
		if (!strcmp(argv[1], "set")) {
			int i;

			for (i = 2; i < argc; i++) {
				char *eq = strchr(argv[i], '=');
				char key[32];
				int val;

				if (!eq) {
					fputs("perfectsense: expected key=value\n",
					      stderr);
					return 1;
				}
				*eq = '\0';
				snprintf(key, sizeof(key), "%s", argv[i]);
				val = atoi(eq + 1);
				*eq = '=';
				if (!strcmp(key, "engage_c"))
					cfg.engage_c = val;
				else if (!strcmp(key, "hysteresis_c"))
					cfg.hysteresis_c = val;
				else if (!strcmp(key, "hold_sec"))
					cfg.hold_sec = val;
				else if (!strcmp(key, "interval_ms"))
					cfg.interval_ms = val;
				else if (!strcmp(key, "offset_cpu"))
					cfg.offset_cpu = val;
				else if (!strcmp(key, "offset_gpu"))
					cfg.offset_gpu = val;
				else if (!strcmp(key, "floor_cpu"))
					cfg.floor_cpu = val;
				else if (!strcmp(key, "floor_gpu"))
					cfg.floor_gpu = val;
				else if (!strcmp(key, "enabled"))
					cfg.enabled = val ? 1 : 0;
				else {
					fprintf(stderr,
						"perfectsense: unknown key '%s'\n",
						key);
					return 1;
				}
			}
			err = ps_fan_aggressor_config_save(&cfg);
			if (err)
				return fail(err);
			daemon_fa_reload();
			if (g_json)
				puts("{\"ok\":true}");
			else
				puts("fan aggressor config saved (daemon reloaded)");
			return 0;
		}
		if (!strcmp(argv[1], "on") || !strcmp(argv[1], "off")) {
			cfg.enabled = !strcmp(argv[1], "on");
			err = ps_fan_aggressor_config_save(&cfg);
			if (err)
				return fail(err);
			daemon_fa_reload();
			if (!cfg.enabled)
				(void)ps_fan_set_auto();
			if (g_json)
				printf("{\"ok\":true,\"fan_aggressor\":%s}\n",
				       cfg.enabled ? "true" : "false");
			else
				printf("fan aggressor: %s\n",
				       cfg.enabled ? "on" : "off");
			return 0;
		}
		if (!strcmp(argv[1], "reload")) {
			daemon_fa_reload();
			if (g_json)
				puts("{\"ok\":true}");
			else
				puts("fan aggressor: daemon reload requested");
			return 0;
		}
		fputs("perfectsense: fan aggressor on|off|status|set|reload …\n",
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
