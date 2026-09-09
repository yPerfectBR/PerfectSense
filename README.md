# PerfectSense

Clean, lightweight Linux support for Acer Predator Sense–class hardware on the
**Predator Helios Neo 16 (PHN16-72-74VX)**.

PerfectSense is a **kernel module** plus a small **userspace stack** (library,
CLI, optional system daemon). There is **no GUI**. Desktop shells and scripts
integrate through sysfs, the CLI, and D-Bus events.

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
perfectsensed (system) ──► D-Bus signals ──► UIs / scripts
         │
         ▼
  perfectsense.ko ──► ACPI WMI ──► Embedded Controller
```

| Component | Role |
|-----------|------|
| `perfectsense.ko` | WMI/EC access; sysfs; `platform_profile`; input/uevents |
| `libperfectsense` | Typed helpers over sysfs (C API) |
| `perfectsense` | CLI (human text + `--json`) |
| `perfectsensed` | System daemon: D-Bus + optional FanAggressor timer |

Exclusive ownership of the Acer gaming WMI path: **blacklist** in-tree
`acer-wmi` / conflicting out-of-tree modules when using PerfectSense.

## Status

**Phase 9 in tree:** FanAggressor offset mode in `perfectsensed` (`timerfd` only
while enabled; hysteresis + manual pause). See
[implementation status](docs/en/implementation/README.md).

Further work is tracked in the [roadmap](docs/en/roadmap/roadmap.md).
Portuguese mirrors live under [`docs/pt-BR/`](docs/pt-BR/).

## Build (preview)

Userspace (Meson) and kernel module (DKMS) are planned. Packaging target:
**Arch Linux** (`packaging/arch/`).

```bash
meson setup build && meson compile -C build
./build/perfectsense   # Phase 0 stub message only

# Kernel stub (needs matching kernel headers):
make -C kmod
# DKMS packaging arrives with later phases — see docs/en/roadmap/roadmap.md
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
