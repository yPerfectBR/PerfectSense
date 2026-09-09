# PerfectSense

Clean, lightweight Linux support for Acer Predator Sense–class hardware on the
**Predator Helios Neo 16 (PHN16-72-74VX)**.

PerfectSense is a **kernel module** plus a small **userspace stack** (library,
optional daemon, and a thin CLI). There is **no GUI** and **no D-Bus API**.

**Who talks how:**

| Consumer | Path |
|----------|------|
| **Human** (manual checks) | `/usr/bin/perfectsense` (CLI) |
| **UI / automation** | **sysfs** directly, or **`libperfectsense`** (C) — closest to hardware |

Do **not** shell out to the CLI from a desktop UI; treat the CLI as a flashlight
for the user, not as the integration bus. For live keys, watch udev or the
`PerfectSense Hotkeys` input device.

**License:** [AGPL-3.0-or-later](LICENSE) (userspace/docs). Kernel module tree
`kmod/` is **GPL-2.0-or-later** — see
[licensing notes](docs/en/architecture/licensing.md).

## Goals

- First-class control of thermal profiles, fans, battery/health attrs, RGB, and
  related Predator firmware features on Linux.
- Stable, documented sysfs ABI (`api_version`) for any consumers.
- Event-oriented userspace: no busy loops by default; optional FanAggressor
  offset logic runs **only when enabled**.
- Portable integration surface for future UIs (bar widgets, control centers,
  scripts) without shipping a desktop frontend here.
- Focused on **PHN16-72-74VX**; code kept clean so other Predator V4 relatives
  can be added later without rewriting the core.

## Architecture (high level)

```
perfectsense (CLI) ──► libperfectsense ──► sysfs
perfectsensed (system) ──► hotkeys / AC policy / FanAggressor timer
         │
         ▼
  perfectsense.ko ──► ACPI WMI ──► Embedded Controller
```

| Component | Role |
|-----------|------|
| `perfectsense.ko` | WMI/EC access; sysfs; `platform_profile`; input/uevents |
| `libperfectsense` | Typed helpers over sysfs (C API for UIs/automation) |
| `perfectsense` | **Human** CLI only (status / one-shot checks; not an IPC layer) |
| `perfectsensed` | System daemon: hotkeys, AC demote, optional FanAggressor |

Exclusive ownership of the Acer gaming WMI path: **blacklist** in-tree
`acer-wmi` / conflicting out-of-tree modules when using PerfectSense.

## Status

**Phase 10 + cutover packaging in tree.** See
[implementation status](docs/en/implementation/README.md) and
[cutover guide](docs/en/cutover.md).

Further work is tracked in the [roadmap](docs/en/roadmap/roadmap.md).
Portuguese mirrors live under [`docs/pt-BR/`](docs/pt-BR/).

## IMPORTANT: GPU performance (PHN16-72)

Install **`nvidia-open`** and enable **`nvidia-powerd`** so Dynamic Boost can raise
the dGPU from the default **~80W** ceiling toward **~140W** when needed:

```bash
sudo pacman -S nvidia-open
sudo systemctl enable nvidia-powerd.service
sudo systemctl reboot
```

PerfectSense then toggles the service with thermal profiles: **off** for
`low-power`/`quiet`, **on** from `balanced` upward (Boost stays dynamic).

Packaging target is **Arch Linux** (official repos: `linux`/`linux-headers`,
`nvidia-open`, `nvidia-utils`). Arch-based derivatives may work; they are not
the support focus.

## Build

```bash
meson setup build && meson compile -C build
./build/perfectsense status

# Kernel module (matching headers; on clang-built kernels use LLVM=1):
make -C kmod
# or: make -C kmod LLVM=1

# Local Arch package (cutover from nekro):
cd packaging/arch && PERFECTSENSE_SRC=../.. makepkg -si
# or live from a built tree:
sudo ./scripts/cutover-to-perfectsense.sh
```

## CLI sketch (target)

```bash
perfectsense status
perfectsense status --json
perfectsense mode get|set <profile>
perfectsense fan get|set|auto …
perfectsense battery …
perfectsense rgb …
```

## Manual testing

Each roadmap phase ends with **manual tests** you run on the PHN16-72-74VX.
The implementer will list exact commands after each small feature lands.

## Contributing

AGPL-3.0: modifications and network-facing integrations must keep source available.
Prefer small, tested patches. New models should add DMI allowlist entries and
quirk tables — do not weaken PHN16-72-74VX gating without evidence.
