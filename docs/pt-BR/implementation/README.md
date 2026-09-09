# Estado da implementação

> **Documente apenas o que existe de fato no repositório.**
> Trabalho planejado: [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Estado atual

| Área | Estado |
|------|--------|
| Módulo kernel | **Fase 5:** perfis, fans, battery/misc (API **4**) |
| libperfectsense | **Fase 5:** cliente sysfs |
| CLI | **Fase 5:** status/mode/fan/battery/usb/… + `--json` |
| perfectsensed / D-Bus | Não implementado |
| FanAggressor | Não implementado |
| Pacote Arch | Skeleton PKGBUILD |

## O que já está no repo

### Kernel (`kmod/`, API **4**)

Attrs: perfil térmico, fans, `battery_limiter`, `battery_calibration`,
`usb_charging`, `lcd_override`, `backlight_timeout`, `boot_animation_sound`.

Neste BIOS, `lcd_override` e `backlight_timeout` leem `-1` (encoding
desconhecido / não suportado — igual ao nekro).

### Userspace

```text
meson setup build && meson compile -C build
build/perfectsense [--json] status|battery|usb|lcd|kb-timeout|boot-sound …
```

## Ainda não

RGB/logo, D-Bus, FanAggressor, CPU power.
