# Cutover: nekro → PerfectSense

**Cutover** means making PerfectSense the **only** owner of Acer gaming WMI on
this machine for day-to-day use: unload/blacklist rivals (`nekro_sense`,
`acer-wmi`), load `perfectsense.ko` at boot, and run `perfectsensed`.

Pandora/desktop code is out of scope here; this document is about the hardware
stack in this repository.

## Distro target

**Arch Linux** (official packages) is the supported packaging target:

| Need | Arch package |
|------|----------------|
| Kernel headers for DKMS | `linux-headers` (with `linux`) |
| NVIDIA driver | `nvidia-open` |
| Dynamic Boost daemon | `nvidia-utils` (`nvidia-powerd.service`) |

Arch-based derivatives may reuse the same PKGBUILD, but CachyOS-/derivative-
specific package names are **not** used or supported as first-class deps.

## What the package does

| Piece | Role |
|-------|------|
| `modprobe.d/perfectsense.conf` | `blacklist acer-wmi` + `blacklist nekro_sense` |
| `modules-load.d/perfectsense.conf` | `modprobe perfectsense` at boot |
| `perfectsensed.path` | Start daemon when sysfs appears |
| `perfectsensed.service` | D-Bus + hotkeys + FanAggressor + powerd sync |
| `Conflicts`/`Replaces` | Avoid packing alongside `nekro-sense` / `linuwu-sense` |
| `post_install` | DKMS build, unload rivals, load module, **enable** units |

## Permanent cutover (recommended)

```bash
cd /path/to/PerfectSense/packaging/arch
PERFECTSENSE_SRC=../.. makepkg -si
```

Then verify:

```bash
lsmod | grep -E 'perfect|nekro'
systemctl is-enabled perfectsensed.path perfectsensed.service
systemctl status perfectsensed --no-pager | head
/usr/bin/perfectsense status
/usr/bin/perfectsense gpu powerd get
```

**Expected:** only `perfectsense` in `lsmod`; daemon active; `api_version` 6.

If `perfectsense` on your `PATH` still resolves to a Pandora/`~/.local/bin`
shim, call `/usr/bin/perfectsense` explicitly (or remove/rename the local shim).

GPU Dynamic Boost prerequisite (once per machine):

```bash
sudo pacman -S nvidia-open
sudo systemctl enable nvidia-powerd.service
```

PerfectSense will **stop** `nvidia-powerd` on eco/quiet and **start** it from
balanced upward.

## Live / dev cutover (without pacman)

From a built tree:

```bash
sudo ./scripts/cutover-to-perfectsense.sh
```

Rollback to nekro (dev drop-ins only):

```bash
sudo ./scripts/rollback-to-nekro.sh
```

If the Arch package is installed, full rollback is:

```bash
sudo pacman -Rns perfectsense
sudo modprobe nekro_sense
```

## Safety

- Only one WMI gaming driver may own the notify GUID / EC path.
- After cutover, reboot once so blacklist + modules-load apply cleanly.
- Keep matching **Arch** `linux` + `linux-headers` so DKMS can build
  `perfectsense.ko` (not derivative-specific header packages as the documented
  path).
