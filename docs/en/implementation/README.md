# Implementation status

> **Only document what is actually present in the repository.**
> Planned work belongs in [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Current status

| Area | State |
|------|-------|
| Kernel module | **Phase 3:** profiles + fans (duty live / target set/auto) |
| libperfectsense | **Phase 4:** sysfs client library |
| CLI | **Phase 4:** `status` / `mode` / `fan` + `--json` |
| perfectsensed / D-Bus | Not implemented |
| FanAggressor | Not implemented |
| Arch package | PKGBUILD skeleton (not installable) |

## Shipped today

### Kernel (`kmod/`, API version **3**)

Unload conflicting Acer gaming WMI modules before `insmod`.

| Sysfs | Access | Meaning |
|-------|--------|---------|
| `…/api_version` | RO | `3` |
| `…/thermal_profile` | RW | Profile name |
| `…/thermal_profile_raw` | RO | Firmware code |
| `…/ac_online` | RO | AC per firmware |
| `…/fan_duty` | RO | Live CPU,GPU duty `%` from firmware (method 17) |
| `…/fan_speed` | RW | Target `cpu,gpu` (`0,0` = auto; `100,100` = max behaviour) |
| `/sys/firmware/acpi/platform_profile` | RW | Class `perfectsense` |

**Fan notes**

- `fan_duty` is live EC/firmware reading; it can lag after switching to auto.
- `fan_speed` stores the last userspace target (`0,0` after auto).
- Module unload restores fan auto.
- Entering `quiet` / `low-power` also forces fan auto.

**AC policy:** on battery, `quiet` / `balanced-performance` / `performance` → `-EOPNOTSUPP`.

### Userspace (Meson)

Build: `meson setup build && meson compile -C build`

| Binary / lib | Role |
|--------------|------|
| `libperfectsense` | Reads/writes sysfs; no daemon |
| `perfectsense` | CLI over the library |

```text
perfectsense [--json] status
perfectsense [--json] mode get|set <name>
perfectsense [--json] fan get|set <cpu> <gpu>|auto
```

Mode names match sysfs (`low-power`, `quiet`, `balanced`,
`balanced-performance`, `performance`). Aliases: `eco`, `turbo`, `bp`.

Override path: `PERFECTSENSE_SYSFS`.

Writes need root or a future `perfect_sense` group ACL; reads are world-readable
on the default sysfs mode.

## Not yet

RGB, battery attrs, D-Bus, FanAggressor, CPU power (Phases 5+ / 7+).
