# Reverse engineering

This section records **methods** and **findings** for understanding the
notebook firmware interfaces used by PerfectSense. It does not track product
roadmaps ([see roadmap](../roadmap/roadmap.md)) or shipped APIs
([see implementation](../implementation/README.md)).

## Methodology (overview)

Work proceeds from passive observation to carefully validated writes:

1. **Identity** — Confirm DMI product (`PHN16-72` / variant string) and BIOS
   version before assuming method layouts.
2. **ACPI inventory** — Dump and disassemble tables (`acpidump` → `acpixtract`
   → `iasl -d`). Locate WMI GUIDs and ASL dispatchers that forward gaming /
   lighting / thermal calls.
3. **Method map** — Enumerate numeric method IDs and buffer sizes used for
   get/set pairs (fans, misc/thermal profile, RGB, battery health, etc.).
4. **Trace validation** — Where available, capture OS-level WMI traces on a
   reference OS to compare buffer layouts with Linux probes.
5. **Conservative getters** — Implement and test **read** paths before any
   **write** path. Log firmware error codes; never guess buffer lengths.
6. **Gated writes** — Apply sets only after DMI allowlisting, size checks, and
   known-good payloads. Prefer restore-to-auto for fans on failure/unload.
7. **Cross-check sensors** — Correlate EC-facing controls with hwmon
   temperatures and fan RPM so userspace policies are evidence-based.

## Safety rules

- Treat every WMI set as potentially sticky across reboot until proven
  otherwise.
- Do not stress-test thermal or battery health controls on battery without a
  recovery plan.
- Keep debug probes out of production paths; production code uses fixed,
  documented payloads.

## Traces and artifacts

Raw traces (when present) live under [`traces/`](traces/). Describe capture
steps alongside each file (tool, host OS, BIOS version, what UI action was
performed). Prefer anonymized / hardware-relevant data only.

## Firmware behavior notes (PHN16-72 class)

### Verified in Phase 1–2

| Item | Value |
|------|-------|
| DMI product name (observed) | `Predator PHN16-72` |
| Gaming WMI GUID | `7A4DDFE7-5B5D-40B4-8595-4408E0CC7F56` |
| Get misc setting method ID | `23` |
| Set misc setting method ID | `22` |
| Get sys info method ID | `5` |
| Sys info cmd: battery/AC status | `0x02` (non-zero ⇒ AC) |
| Misc index: supported profiles | `0x000A` (bit N ⇒ profile code N) |
| Misc index: platform profile | `0x000B` |
| Get packing | index bits 7:0; value bits 15:8; status bits 7:0 must be 0 |
| Set packing | index bits 7:0 + value bits 15:8; status bits 7:0 must be 0 |

Thermal profile firmware codes → PerfectSense / `platform_profile` names:

| Code | Name |
|------|------|
| `0x06` | `low-power` (Eco) |
| `0x00` | `quiet` |
| `0x01` | `balanced` |
| `0x04` | `balanced-performance` |
| `0x05` | `performance` (Turbo) |

On battery, OEM policy blocks `quiet`, `balanced-performance`, and `performance`.

### Fans (Phase 3)

| Item | Value |
|------|-------|
| Set fan behaviour method | `14` |
| Set fan speed method | `16` |
| Get fan speed (live duty) method | `17` |
| CPU fan id | `0x01` |
| GPU fan id | `0x04` |
| Auto behaviour payload | `0x410009` |
| Max behaviour payload | `0x820009` |
| Mixed custom behaviour | `0xC30009` then per-fan method 16 |
| Speed encode | `((pct * 25600) / 100) & 0xFF00) + fan_id` |

Sysfs: `fan_duty` = live get; `fan_speed` = last target (`0,0` auto).

### Battery / misc (Phase 5)

| Item | Value |
|------|-------|
| Battery health GUID | `79772EC5-04B1-4bfd-843C-61E7F77B6CC9` (GUID5) |
| Get / set battery health methods | `20` / `21` |
| Health mode / calibration mode | `1` / `2` (function mask) |
| Boot animation sound | misc index `0x06` (GUID4 methods 23/22) |
| ApgeAction GUID | `61EF69EA-865C-4BC3-A502-A0DEBA0CB531` (GUID3) |
| USB charging get/set | Apge methods `2`/`1` with fixed payloads (0/10/20/30 min) |
| KB backlight timeout | Apge get `0x88401` / set `0x88402` family |
| LCD override | GUID4 gaming profile get `3` / set `1` |

**PHN16-72 decode notes (validated):**

| Attr | Raw observation | Decode |
|------|-----------------|--------|
| `lcd_override` | off `0xff000101ff00`, on `0x1ff000101ff00` | bit **48** (`0x1000000000000`); set payloads unchanged |
| `backlight_timeout` | off `0x80000` (or `0` before first set), on `0x1e0000080000` | exact match + bit family `0x1e0000000000` |

Older PHN16-71 magic equality (`lcd` `0x1000001000000` / `0x1000000`) still agrees with the bit-48 test.
USB charging and battery health decode correctly.

### RGB keyboard + lid logo (Phase 6)

| Item | Value |
|------|-------|
| Set / get per-zone RGB methods | `6` / `7` (GUID4); zone masks `0x1,0x2,0x4,0x8` |
| Per-zone set payload | 4-byte buffer `{zone,R,G,B}` (not packed u64) |
| KB backlight set / get | methods `20` / `21` (16-byte buffers) |
| Zone enable | method `2` with `8 \| (15 << 40)` after sysinfo get |
| Logo set / get | methods `12` / `13` (LB select byte + RGB/bri/en) |
| Sysfs | `four_zoned_kb/per_zone_mode`, `four_zone_mode`; `back_logo/color` |

Validated on PHN16-72: per-zone round-trip and logo on/off.

### Hotkeys / AC events (Phase 10)

| Item | Value |
|------|-------|
| Event GUID | `676AA15E-6A47-4D9F-A2CC-1E6D18D14026` |
| Turbo / mode notify function | `0x07` |
| AC notify function | `0x08` |
| Event buffer | 8-byte packed (`function`, `key_num`, …) |
| PHN16-72 turbo key | `key_num == 0x4` |
| PHN16-72 thermal mode key | `key_num == 0x5` |
| AC offline / online | `key_num` `0` / `1` |

Kernel surfaces input (`KEY_PROG1` / `KEY_PROG2`) and uevent env
`PERFECTSENSE_EVENT` / `PERFECTSENSE_KEY`. Only one driver may own the notify
handler on this GUID.

### Sensors / hwmon

| Item | Value |
|------|-------|
| Supported sensors cmd | `GET_SYS_INFO` with `0x0000` |
| Sensor reading cmd | `0x0001` + index in bits 15:8 |
| Reading value | bits 23:8 |
| Supported mask | bits 39:24 |
| IDs | CPU temp `0x01`, CPU fan `0x02`, GPU fan `0x06`, GPU temp `0x0A` |
| hwmon name | `perfectsense` (`temp1/2`, `fan1/2`) |

## Related docs

- [Architecture](../architecture/overview.md) — how PerfectSense uses these
  findings in a kernel module + userspace design
