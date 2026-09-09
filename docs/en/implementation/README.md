# Implementation status

> **Only document what is actually present in the repository.**
> Planned work belongs in [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Current status

| Area | State |
|------|-------|
| Kernel module | **Phase 5:** profiles, fans, battery/misc attrs (API **4**) |
| libperfectsense | **Phase 5:** sysfs client including Phase 5 attrs |
| CLI | **Phase 5:** `status` / `mode` / `fan` / battery / usb / lcd / … + `--json` |
| perfectsensed / D-Bus | Not implemented |
| FanAggressor | Not implemented |
| Arch package | PKGBUILD skeleton (not installable) |

## Shipped today

### Kernel (`kmod/`, API version **4**)

Unload conflicting Acer gaming WMI modules before `insmod`.

| Sysfs | Access | Meaning |
|-------|--------|---------|
| `…/api_version` | RO | `4` |
| `…/thermal_profile` | RW | Profile name |
| `…/thermal_profile_raw` | RO | Firmware code |
| `…/ac_online` | RO | AC per firmware |
| `…/fan_duty` | RO | Live CPU,GPU duty `%` |
| `…/fan_speed` | RW | Target `cpu,gpu` (`0,0` = auto) |
| `…/battery_limiter` | RW | `0`/`1` — ~80% charge health limit |
| `…/battery_calibration` | RW | `0`/`1` — calibration mode (use carefully) |
| `…/usb_charging` | RW | `0`/`10`/`20`/`30` minutes (or `-1` unknown) |
| `…/lcd_override` | RW | `0`/`1` (or `-1` if firmware encoding unknown) |
| `…/backlight_timeout` | RW | `0`/`1` KB 30s timeout (or `-1`) |
| `…/boot_animation_sound` | RW | `0`/`1` |
| `/sys/firmware/acpi/platform_profile` | RW | Class `perfectsense` |

On PHN16-72-74VX, `lcd_override` and `backlight_timeout` currently read as
`-1` (same as nekro_sense on this BIOS); get/set payloads are still wired for
known encodings.

**Fan notes:** live `fan_duty` can lag after auto; unload restores auto;
`quiet` / `low-power` force auto.

**AC policy:** on battery, `quiet` / `balanced-performance` / `performance` →
`-EOPNOTSUPP`.

### Userspace (Meson)

```text
meson setup build && meson compile -C build
LD_LIBRARY_PATH=build build/perfectsense [--json] status
build/perfectsense battery get|on|off
build/perfectsense battery calibration get|on|off
build/perfectsense usb get|set <0|10|20|30>
build/perfectsense lcd|kb-timeout|boot-sound get|set …
```

Writes need root (or a future group ACL).

## Not yet

RGB/logo, D-Bus, FanAggressor, CPU power (Phases 6+).
