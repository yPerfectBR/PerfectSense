# Licensing notes

The repository root [LICENSE](../../../LICENSE) is **AGPL-3.0** for the
project as published (documentation, userspace library, CLI, daemon, packaging
scripts).

## Kernel module exception (required for Linux)

Out-of-tree Linux kernel modules that call **GPL-only** kernel exports must use
a kernel-accepted `MODULE_LICENSE` that is GPL-compatible (typically
`MODULE_LICENSE("GPL")` with source under **GPL-2.0-or-later** for the `kmod/`
tree).

Therefore PerfectSense uses:

| Tree | License |
|------|---------|
| `kmod/` | **GPL-2.0-or-later** (Linux module practice) |
| Userspace (`lib/`, `cli/`, `daemon/`), docs, packaging | **AGPL-3.0-or-later** |

This keeps the driver loadable and linkable against the kernel while preserving
strong copyleft for the integration stack and tools. Contributors patching
`kmod/` agree those files are GPL-2.0-or-later; everything else remains AGPL-3.0
unless a file header says otherwise.

If this dual arrangement is unacceptable for a contribution, discuss before
merging — a pure-AGPL `.ko` is not practical on mainline Linux.
