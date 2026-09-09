# Implementation roadmap

> **This document is a plan.** It describes intended phases and acceptance
> ideas. It is **not** a changelog of shipped features. For what actually works
> in the tree today, see [implementation/](../implementation/README.md).

Target hardware for all phases: **Acer Predator Helios Neo 16 PHN16-72-74VX**
on **Arch Linux**.

Each phase ends with **manual tests** on the machine. The implementer will ask
you to run specific commands after each small feature.

## Phase 0 — Repository skeleton

- Meson userspace project stub (`libperfectsense`, `perfectsense`, `perfectsensed`)
- DKMS stub for `perfectsense.ko`
- Arch `packaging/arch/` PKGBUILD skeleton
- Doc trees `docs/en` + `docs/pt-BR`
- Module blacklist notes for conflicting WMI drivers

**Exit:** project builds empty/stub targets; docs render; license AGPL-3.0.

## Phase 1 — Kernel probe + API version + profile get

- DMI gate for PHN16-72-74VX
- WMI GUID bind / probe
- Sysfs `perfect_sense/api_version`
- Read thermal / platform profile (get path only)

**Manual tests (examples):** module loads; `api_version` readable; profile get
matches firmware expectation without writes.

## Phase 2 — Profile set + `platform_profile`

- Set thermal profile via WMI
- Register `platform_profile` choices/mapping (Eco…Turbo class mapping)
- Safe failure when on battery for unsupported high profiles (as firmware requires)

**Manual tests:** set each supported profile; verify sysfs + observable fan/LED
behavior; revert to balanced.

## Phase 3 — Fans get / set / auto

- Gaming fan speed get (live duty) and set
- Auto mode (return control to firmware)
- Sysfs attributes with clear formats (`cpu,gpu` duties)

**Manual tests:** read duty in auto; set fixed duties; return to auto; no stuck
100% on unload (restore auto on exit where possible).

## Phase 4 — Library + CLI (`status` / `mode` / `fan`)

- `libperfectsense` over sysfs
- CLI human output + **`--json`** for automation
- No daemon required for these commands

**Manual tests:** `perfectsense status --json` parses; mode/fan round-trips.

## Phase 5 — Battery and misc attrs

- Battery limiter / related health controls as supported
- USB charging, LCD override, backlight timeout, boot animation sound
- Sysfs parity with a documented attribute table

**Manual tests:** each attr get/set with known-safe values; reboot persistence
notes where applicable.

## Phase 6 — RGB keyboard + lid logo

- Four-zone KB get/set
- Back logo / lightbar get/set
- Reset/off helpers

**Manual tests:** per-zone colors; brightness; logo on/off; no firmware reject
spam in `dmesg`.

## Phase 7 — `perfectsensed` (note: D-Bus removed)

Original plan exported `org.perfectsense.Sense1`. As of **0.0.18** the daemon
has **no D-Bus API**; control is sysfs/CLI only. Daemon retains hotkey/AC/FA.

## Phase 7 (legacy plan) — `perfectsensed` + D-Bus (no FanAggressor yet)

- Systemd **system** unit (root)
- `org.perfectsense.Sense1` properties + signals (profile, basic state)
- CLI still works without daemon

**Manual tests:** `busctl` introspect; signal on mode change; stop daemon and
confirm CLI still functions.

## Phase 8 — CPU power controls

- Governor, turbo, EPP, RAPL PL1/PL2, max frequency helpers
- Optional named presets composed with platform profile
- Still event/command driven (no thermal loop)

**Manual tests:** apply/revert presets; verify sysfs `powercap`/`cpufreq`; idle
power sanity.

## Phase 9 — FanAggressor (offset mode)

- Enable/disable offset mode via D-Bus/CLI/config
- `timerfd` loop **only while enabled**
- Hybrid engage/disengage thresholds + hysteresis; per-axis offsets
- Disable → cancel timer → fans auto

**Manual tests:** enable under load; confirm offset; disable restores auto; CPU
idle with feature off shows no periodic wakeups from the daemon timer.

## Phase 10 — Turbo/mode key events + polish

- Kernel uevent/input for hardware keys
- Daemon → D-Bus signals
- Man pages, PKGBUILD completeness, hardening review

**Manual tests:** key press (or `inject_event`) emits D-Bus `Hotkey`; package
builds from a local checkout via `makepkg`.

## Out of scope (this repository)

- Shipping a GUI
- Maintaining CachyOS-specific packages
- Changing the separate Pandora desktop stack
- Guaranteeing non–PHN16-72-74VX models (clean hooks only)

## Ordering rationale

Profiles and fans first prove the WMI path with minimal surface. Misc attrs and
RGB follow once probe/set patterns are solid. D-Bus comes after a working CLI so
UIs are not blocked on daemon bugs. CPU power is userspace-only and lands before
FanAggressor so RAPL/EPP can be validated without a thermal timer. FanAggressor
is last among core features because it is the only intentional periodic work.
