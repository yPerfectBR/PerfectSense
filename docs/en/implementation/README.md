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
| Packaging | Cutover: blacklist + modules-load + enable units + Conflicts |

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

Daemon: `Hotkey(s,u)`; mode cycles (skips rejected); turbo AC-only toggle;
**AC offline demotes** `quiet` / `balanced-performance` / `performance` →
`balanced` (restores on plug-in when possible).
Debug: `perfectsense event inject <name>` or sysfs `inject_event`.

## FanAggressor

Default **off**. Config: `/etc/perfectsense/fan-aggressor.conf` (persist) +
`/run/perfectsense/…` (override).

```text
perfectsense fan aggressor status|on|off|set engage_c=88 offset_cpu=15 …
perfectsense fan aggressor reload   # re-read conf in perfectsensed
```

`set` / `on` / `off` persist conf and call D-Bus `FanAggressorReload` so the
daemon picks up engage/offset/interval without restart.

## Cutover notes

See **[cutover.md](../cutover.md)** for the full nekro → PerfectSense switch.

Packaging installs and enables:

- `modprobe.d`: blacklist `acer-wmi` + `nekro_sense`
- `modules-load.d`: load `perfectsense`
- `perfectsensed.service` + `.path` (**enabled** on package install)
- `Conflicts`/`Replaces` vs `nekro-sense` / `linuwu-sense`

Dev helpers: `scripts/cutover-to-perfectsense.sh`, `scripts/rollback-to-nekro.sh`
(also installed as `perfectsense-cutover` / `perfectsense-rollback`).

## IMPORTANT: GPU performance (PHN16-72)

The discrete NVIDIA GPU defaults to an **~80W** ceiling. Enabling
`nvidia-powerd` (Dynamic Boost) allows up to **~140W** when the workload needs
it.

```bash
sudo pacman -S nvidia-open
sudo systemctl enable nvidia-powerd.service
sudo systemctl reboot
```

PerfectSense then **starts/stops** `nvidia-powerd` with the thermal profile:

| Profile | `nvidia-powerd` |
|---------|-----------------|
| `low-power` / `quiet` | **off** (~80W) |
| `balanced` and above | **on** (Dynamic Boost) |

```text
perfectsense gpu powerd get|on|off
```
