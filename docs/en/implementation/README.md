# Implementation status

> **Only document what is actually present in the repository.**
> Planned work belongs in [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Current status

| Area | State |
|------|-------|
| Kernel module | **Phase 1:** DMI gate, WMI misc get, sysfs `perfect_sense/` |
| libperfectsense | Placeholder directory |
| CLI | Phase 0 stub binary |
| perfectsensed / D-Bus | Not implemented |
| FanAggressor | Not implemented |
| Arch package | PKGBUILD skeleton (not installable) |

## Shipped today

### Kernel (`kmod/`, API version **1**)

Requires unloading conflicting Acer gaming WMI modules first (e.g. out-of-tree
alternatives). Paths after load:

| Sysfs | Access | Meaning |
|-------|--------|---------|
| `/sys/devices/platform/perfectsense/perfect_sense/api_version` | RO | ABI version (`1`) |
| `…/thermal_profile` | RO | Mapped name (`low-power`, `quiet`, `balanced`, `balanced-performance`, `performance`) |
| `…/thermal_profile_raw` | RO | Firmware code (`0x00`…`0x06`) |

Profile name mapping matches future `platform_profile` vocabulary (Phase 2).

### Other

- AGPL-3.0 userspace/docs; `kmod/` GPL-2.0-or-later
- Docs (`docs/en`, `docs/pt-BR`)
- Meson CLI stub
- Packaging placeholders

## Not yet

Profile **set**, `platform_profile` class registration, fans, RGB, D-Bus, etc.
(see roadmap Phases 2+).
