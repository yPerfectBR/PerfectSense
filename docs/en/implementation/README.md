# Implementation status

> **Only document what is actually present in the repository.**
> Planned work belongs in [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Current status

| Area | State |
|------|-------|
| Kernel module | **Phase 6:** API **5** (profiles, fans, misc, RGB/logo) |
| libperfectsense | **Phase 6+** sysfs client |
| CLI | Sysfs one-shot (works **without** daemon) |
| perfectsensed / D-Bus | **Phase 7:** `org.perfectsense.Sense1` |
| FanAggressor | Not implemented |
| Arch package | PKGBUILD skeleton (not installable) |

## Shipped today

### Kernel (`kmod/`, API **5**)

Sysfs groups under `/sys/devices/platform/perfectsense/`:

- `perfect_sense/` — thermal, fans, battery/misc, `lighting_reset`
- `four_zoned_kb/` — `per_zone_mode`, `four_zone_mode`
- `back_logo/color`

### Userspace

```text
meson setup build && meson compile -C build
build/perfectsense …          # sysfs CLI
build/perfectsensed           # system D-Bus daemon (root)
```

Install snippets (Meson `install`):

- `lib/systemd/system/perfectsensed.service`
- `share/dbus-1/system.d/org.perfectsense.Sense1.conf`
- `share/dbus-1/interfaces/org.perfectsense.Sense1.xml`

### D-Bus (`org.perfectsense.Sense1`)

| Item | Value |
|------|-------|
| Bus name | `org.perfectsense.Sense1` |
| Object path | `/org/perfectsense/Sense1` |
| Interface | `org.perfectsense.Sense1` |

**Properties (selected):** `ApiVersion`, `Available`, `ThermalProfile` (RW),
`PlatformProfile`, `AcOnline`, fan duty/target/auto, `BatteryLimiter`,
`UsbCharging`, `Rgb`, `Logo`.

**Methods:** `SetFanTarget(uu)`, `SetFanAuto()`, `SetRgb(s)`, `SetLogo(s)`,
`Refresh()`.

**Signals:** `ProfileChanged(s)` — emitted when `ThermalProfile` is set via D-Bus.

```text
busctl introspect org.perfectsense.Sense1 /org/perfectsense/Sense1 org.perfectsense.Sense1
busctl set-property … ThermalProfile s balanced
```

Requires `perfectsense.ko` loaded. Stopping the daemon does **not** break the CLI.

## Not yet

FanAggressor, CPU power presets, hardware key → D-Bus (Phases 8–10).
