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

### Verified in Phase 1 (read path)

| Item | Value |
|------|-------|
| DMI product name (observed) | `Predator PHN16-72` |
| Gaming WMI GUID | `7A4DDFE7-5B5D-40B4-8595-4408E0CC7F56` |
| Get misc setting method ID | `23` |
| Misc index: platform profile | `0x000B` |
| Input packing | index in bits 7:0; value returned in bits 15:8; status bits 7:0 of result must be 0 |

Thermal profile firmware codes → PerfectSense names:

| Code | Name |
|------|------|
| `0x06` | `low-power` (Eco) |
| `0x00` | `quiet` |
| `0x01` | `balanced` |
| `0x04` | `balanced-performance` |
| `0x05` | `performance` (Turbo) |

### Still to document as phases land

- Set misc method (`22`) and AC-only restrictions for aggressive modes
- Fan auto vs manual duty; live duty read vs last-written target
- Four-zone keyboard and lid logo payload sizes / selectors
- Battery charge limit semantics
- Hardware turbo / mode key event codes

## Related docs

- [Architecture](../architecture/overview.md) — how PerfectSense uses these
  findings in a kernel module + userspace design
