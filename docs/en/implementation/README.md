# Implementation status

> **Only document what is actually present in the repository.**
> Planned work belongs in [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Current status

| Area | State |
|------|-------|
| Kernel module | API **6** + **hwmon**; LCD/kb-timeout decode fixed for PHN16-72 |
| libperfectsense | temps/RPM, RGB effect, event inject, fan curve |
| CLI | `temps`, `rgb effect`, `event inject`, `fan curve` |
| perfectsensed | Hotkey + AC demote + optional fan curve (**no D-Bus**; SIGHUP reloads curve) |
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

## Fan curve

Default **off**. Config: `/etc/perfectsense/fan-curve.conf` (persist) +
`/run/perfectsense/…` (override).

```text
perfectsense fan curve status|on|off|set points=40:25:20,75:50:45,95:100:90 …
perfectsense fan curve set interval_ms=1500
perfectsense fan curve reload   # re-read conf in perfectsensed
```

`set` / `on` / `off` persist conf and send **SIGHUP** to `perfectsensed`.
Points are `temp:cpu%:gpu%` with linear interpolation (CPU duty←CPU temp,
GPU duty←GPU temp). Manual `fan set` pauses the curve until auto/curve resumes.

## Control plane

| Who | How |
|-----|-----|
| Human | `/usr/bin/perfectsense` |
| UI / scripts | **sysfs** or **`libperfectsense`** (do not shell the CLI) |
| Live keys | udev / input `PerfectSense Hotkeys` |

`perfectsensed` only applies policy (hotkeys, AC, FA); it is not an RPC bus.

## GPU / nvidia-powerd sync

Thermal profiles expect Dynamic Boost on/off. UIs should check:

| Field / API | Meaning |
|-------------|---------|
| `ps_status.nvidia_powerd_synced` | `0` = warn user (often start-limit) |
| `ps_mode_set()` → `-EBUSY` | Profile applied, powerd sync failed |
| `ps_gpu_powerd_synced()` | Poll without changing anything |

`ps_strerror(-EBUSY)` describes the recovery command.

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
