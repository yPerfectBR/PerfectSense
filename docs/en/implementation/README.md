# Implementation status

> **Only document what is actually present in the repository.**
> Planned work belongs in [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Current status

| Area | State |
|------|-------|
| Kernel module | Phase 0 stub (`kmod/`) — loads message only, no WMI |
| libperfectsense | Placeholder directory |
| CLI | Phase 0 stub binary (`perfectsense` prints help pointer) |
| perfectsensed / D-Bus | Not implemented |
| FanAggressor | Not implemented |
| Arch package | PKGBUILD skeleton (not installable) |

## Shipped today

- Repository license: AGPL-3.0 (userspace/docs); `kmod/` GPL-2.0-or-later
- Design, roadmap, RE methodology docs (`docs/en`, `docs/pt-BR`)
- Meson project with stub CLI
- DKMS/modprobe packaging placeholders

As features land, this page will list sysfs attributes, CLI subcommands, and
D-Bus interfaces with versions — and will stop claiming anything that is not
buildable/testable on PHN16-72-74VX.
