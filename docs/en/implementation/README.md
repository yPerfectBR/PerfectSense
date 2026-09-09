# Implementation status

> **Only document what is actually present in the repository.**
> Planned work belongs in [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Current status

| Area | State |
|------|-------|
| Kernel module | **Phase 10:** API **6** (hotkeys/AC uevent + input) |
| libperfectsense | **Phase 9+:** temps + FanAggressor config |
| CLI | `status` / `mode` / `fan` / misc / RGB / `cpu` / aggressor |
| perfectsensed / D-Bus | **Phase 10:** `Hotkey` signal + mode/turbo handling |
| Man pages | `perfectsense(1)`, `perfectsensed(8)` |
| Arch package | Installable `packaging/arch/PKGBUILD` (local tree / `PERFECTSENSE_SRC`) |

## Hotkeys (Phase 10)

Kernel owns WMI notify GUID `676AA15E-6A47-4D9F-A2CC-1E6D18D14026`:

| Event | Input | Uevent |
|-------|-------|--------|
| Turbo key (`0x4`) | `KEY_PROG1` | `PERFECTSENSE_EVENT=turbo` |
| Mode key (`0x5`) | `KEY_PROG2` | `PERFECTSENSE_EVENT=mode` |
| AC online/offline | — | `ac-online` / `ac-offline` |

`perfectsensed` watches platform uevents and emits:

```text
org.perfectsense.Sense1.Hotkey (s event, u key)
```

Default userspace actions:

- **mode** — cycle thermal profiles (`low-power` → `quiet` → `balanced` →
  `balanced-performance` → `performance`), **skipping** profiles rejected by
  firmware (e.g. aggressive modes on battery)
- **turbo** — AC only; toggles CPU preset `turbo` (with platform sync). Second
  press restores the CPU snapshot and returns thermal to `balanced`

Debug (WO): `…/perfect_sense/inject_event` ← `turbo` | `mode` | `ac-online` |
`ac-offline` (synthetic uevent/input for tests).

Only one driver may install the WMI notify handler.

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

**Note:** acer `hwmon` may come from another Acer WMI module. With only
`perfectsense.ko`, CPU temp falls back to coretemp; GPU may read 0.

## Packaging

```text
cd packaging/arch
PERFECTSENSE_SRC=../.. makepkg -si
```

DKMS sources install under `/usr/src/perfectsense-$pkgver`. Enable
`perfectsensed` after `modprobe perfectsense`.

## Prior highlights

- Sense1 D-Bus, CPU presets, RGB/logo, battery/misc, profiles/fans.
