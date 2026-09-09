# Arch Linux packaging

Support target: **Arch Linux** (official repositories).

| Dependency | Arch package |
|------------|--------------|
| DKMS | `dkms` |
| Kernel headers | `linux-headers` (pair with `linux`) |
| NVIDIA (optional) | `nvidia-open` + `nvidia-utils` |

Do **not** list derivative-specific packages (`linux-cachyos-headers`, etc.) as
dependencies. Those distros can map to their own headers locally; the documented
and tested packaging contract is stock Arch.

```bash
PERFECTSENSE_SRC=../.. makepkg -si
```

See [cutover](../../docs/en/cutover.md).
