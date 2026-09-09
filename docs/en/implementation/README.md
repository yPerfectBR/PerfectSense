# Implementation status

> **Only document what is actually present in the repository.**
> Planned work belongs in [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Current status

| Area | State |
|------|-------|
| Kernel module | **Phase 2:** profile get/set, `platform_profile`, AC gate |
| libperfectsense | Placeholder directory |
| CLI | Phase 0 stub binary |
| perfectsensed / D-Bus | Not implemented |
| FanAggressor | Not implemented |
| Arch package | PKGBUILD skeleton (not installable) |

## Shipped today

### Kernel (`kmod/`, API version **2**)

Unload conflicting Acer gaming WMI modules before `insmod`.

| Sysfs | Access | Meaning |
|-------|--------|---------|
| `/sys/devices/platform/perfectsense/perfect_sense/api_version` | RO | ABI version (`2`) |
| `…/thermal_profile` | RW | Profile name (see table) |
| `…/thermal_profile_raw` | RO | Firmware code |
| `…/ac_online` | RO | `1` if AC per firmware, else `0` |
| `/sys/firmware/acpi/platform_profile` | RW | Class device registered as `perfectsense` |
| `/sys/firmware/acpi/platform_profile_choices` | RO | Supported profiles |

| Firmware | Name |
|----------|------|
| `0x06` | `low-power` |
| `0x00` | `quiet` |
| `0x01` | `balanced` |
| `0x04` | `balanced-performance` |
| `0x05` | `performance` |

**AC policy:** on battery, setting `quiet`, `balanced-performance`, or `performance`
returns `-EOPNOTSUPP`. `low-power` and `balanced` remain available.

Quiet/Eco no longer force fan auto here (fans arrive in Phase 3).

### Other

- AGPL-3.0 userspace/docs; `kmod/` GPL-2.0-or-later
- Docs (`docs/en`, `docs/pt-BR`)
- Meson CLI stub
- Packaging placeholders

## Not yet

Fans, RGB, D-Bus, FanAggressor, CPU power (Phases 3+).
