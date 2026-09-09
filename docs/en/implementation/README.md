# Implementation status

> **Only document what is actually present in the repository.**
> Planned work belongs in [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Current status

| Area | State |
|------|-------|
| Kernel module | **Phase 6:** API **5** |
| libperfectsense | **Phase 9:** + temps + FanAggressor config |
| CLI | + `fan aggressor` / `cpu` |
| perfectsensed / D-Bus | **Phase 9:** timerfd offset mode |
| Arch package | PKGBUILD skeleton |

## FanAggressor (Phase 9)

**Default off.** No `timerfd` while disabled.

When enabled in `perfectsensed`:

1. Poll temps every `interval_ms` (acer hwmon + coretemp package).
2. If max(temp) ≥ `engage_c` for `hold_sec`, sample live duty and apply
   `max(floor, native + offset)` per axis (capped at 100).
3. Disengage when temp ≤ `engage_c - hysteresis_c` → fans auto (`0,0`).
4. Non-auto fan targets from elsewhere pause control (manual).

```text
perfectsense fan aggressor status|on|off
busctl set-property … FanAggressorEnabled b true
busctl call … FanAggressorStatus
```

Config: `/run/perfectsense/fan-aggressor.conf`  
Defaults: engage 90 °C, hyst 3, hold 15 s, offset 20, floor 55, interval 2 s.

**Note:** acer `hwmon` is provided by nekro/`acer-wmi` today. With only
`perfectsense.ko`, CPU temp falls back to coretemp; GPU may read 0.

## Prior highlights

- Sense1 D-Bus, CPU presets, RGB/logo, battery/misc, profiles/fans (API 5).

## Not yet

Turbo/mode key events, man pages, installable PKGBUILD (Phase 10).
