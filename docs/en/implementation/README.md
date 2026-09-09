# Implementation status

> **Only document what is actually present in the repository.**
> Planned work belongs in [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Current status

| Area | State |
|------|-------|
| Kernel module | **Phase 6:** API **5** (WMI features) |
| libperfectsense | **Phase 8:** sysfs + CPU power |
| CLI | Sysfs + `cpu` presets (no daemon required) |
| perfectsensed / D-Bus | **Phase 7+8:** Sense1 + `CpuPreset` / `CpuRestore` |
| FanAggressor | Not implemented |
| Arch package | PKGBUILD skeleton |

## Shipped today

### CPU power (Phase 8, userspace only)

Reads/writes `cpufreq`, `intel_pstate`, and package RAPL (`intel-rapl:0`).

```text
perfectsense cpu status
perfectsense cpu snapshot          # baseline for restore
perfectsense cpu preset power|balanced|performance|turbo [--sync-profile]
perfectsense cpu restore
perfectsense cpu set governor|epp|turbo|max-perf|pl1|pl2 …
```

| Preset | Governor | EPP | Optional `--sync-profile` |
|--------|----------|-----|---------------------------|
| `power` / `eco` | powersave | power | `low-power` |
| `balanced` | powersave | balance_performance | `balanced` |
| `performance` / `bp` | powersave | performance | `balanced-performance` |
| `turbo` | performance | performance | `performance` |

Snapshot file: `/run/perfectsense/cpu-power.snap` (created on first preset or
`cpu snapshot`; restore returns to that baseline).

D-Bus: properties `CpuGovernor`, `CpuEpp`, `CpuTurbo`, `CpuMaxPerfPct`,
`CpuPl1Uw`, `CpuPl2Uw`; methods `CpuPreset(sb)`, `CpuRestore()`.

### Prior phases

Kernel API 5 + Sense1 daemon as documented previously (thermal, fans, attrs,
RGB/logo).

## Not yet

FanAggressor, hardware key events, packaging polish (Phases 9–10).
