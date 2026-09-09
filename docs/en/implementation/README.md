# Implementation status

> **Only document what is actually present in the repository.**
> Planned work belongs in [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Current status

| Area | State |
|------|-------|
| Kernel module | API **6** + **hwmon** (`perfectsense`) temps/RPM |
| libperfectsense | temps/RPM, RGB effect, event inject, FA etc+run |
| CLI | `temps`, `rgb effect`, `event inject`, `fan aggressor set` |
| perfectsensed | Hotkey (+300 ms debounce), FA timer |
| Packaging | blacklist + `modules-load.d`, path unit, PKGBUILD |

## Sensors (no nekro required)

`perfectsense.ko` registers hwmon name **`perfectsense`**:

| Channel | Meaning |
|---------|---------|
| `temp1` / `temp2` | CPU / GPU (°C × 1000 in sysfs) |
| `fan1` / `fan2` | CPU / GPU RPM |

Userspace prefers `perfectsense`, then legacy `acer`, then coretemp / vendor GPU hwmon.

## Hotkeys

| Event | Input | Uevent |
|-------|-------|--------|
| Turbo (`0x4`) | `KEY_PROG1` | `PERFECTSENSE_EVENT=turbo` |
| Mode (`0x5`) | `KEY_PROG2` | `PERFECTSENSE_EVENT=mode` |
| AC | — | `ac-online` / `ac-offline` |

Daemon: `Hotkey(s,u)`; mode cycles (skips rejected); turbo AC-only toggle.
Debug: `perfectsense event inject <name>` or sysfs `inject_event`.

## FanAggressor

Default **off**. Config: `/etc/perfectsense/fan-aggressor.conf` (persist) +
`/run/perfectsense/…` (override).

```text
perfectsense fan aggressor status|on|off|set engage_c=88 offset_cpu=15 …
```

## Cutover notes

Packaging installs:

- `modprobe.d`: blacklist `acer-wmi` + `nekro_sense`
- `modules-load.d`: load `perfectsense`
- `perfectsensed.service` + `.path`

## NVIDIA PowerMizer (Helios)

Changing thermal profile also sets Preferred Mode via `nvidia-settings`:

| Thermal | PowerMizer |
|---------|------------|
| `low-power` / `quiet` | Normal (0) |
| `balanced` | Auto (2) |
| `balanced-performance` / `performance` | Prefer Maximum (1) |

```text
perfectsense gpu powermizer get|set normal|auto|max
```

On some Wayland sessions NV-CONTROL cannot change the attribute (set appears
to succeed but the value stays); thermal profile still applies.
