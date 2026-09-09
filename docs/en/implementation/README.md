# Implementation status

> **Only document what is actually present in the repository.**
> Planned work belongs in [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Current status

| Area | State |
|------|-------|
| Kernel module | **Phase 6:** profiles, fans, misc attrs, RGB KB + logo (API **5**) |
| libperfectsense | **Phase 6:** sysfs client including RGB/logo |
| CLI | **Phase 6:** + `rgb` / `logo` / `lights reset` + `--json` |
| perfectsensed / D-Bus | Not implemented |
| FanAggressor | Not implemented |
| Arch package | PKGBUILD skeleton (not installable) |

## Shipped today

### Kernel (`kmod/`, API version **5**)

Unload conflicting Acer gaming WMI modules before `insmod`.

**`…/perfect_sense/`** — thermal, fans, battery/misc, `lighting_reset` (WO).

**`…/four_zoned_kb/`**

| Attr | Format |
|------|--------|
| `per_zone_mode` | `RRGGBB,RRGGBB,RRGGBB,RRGGBB,brightness` |
| `four_zone_mode` | `mode,speed,brightness,direction,r,g,b` |

**`…/back_logo/`**

| Attr | Format |
|------|--------|
| `color` | `RRGGBB,brightness[,enable]` (`enable=0` forces brightness 0) |

On PHN16-72-74VX, per-zone RGB get/set and logo get/set validated; `lcd_override`
/ `backlight_timeout` still read `-1`.

### Userspace

```text
meson setup build && meson compile -C build
build/perfectsense rgb get|set <z1> <z2> <z3> <z4> [bri]|off
build/perfectsense logo get|set <RRGGBB> [bri] [en]|off
build/perfectsense lights reset
```

## Not yet

D-Bus / `perfectsensed`, FanAggressor, CPU power, key events (Phases 7+).
