# Estado da implementação

> **Documente apenas o que existe de fato no repositório.**
> Trabalho planejado: [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Estado atual

| Área | Estado |
|------|--------|
| Módulo kernel | **Fase 3:** perfis + fans (duty vivo / target set/auto) |
| libperfectsense | **Fase 4:** cliente sysfs |
| CLI | **Fase 4:** `status` / `mode` / `fan` + `--json` |
| perfectsensed / D-Bus | Não implementado |
| FanAggressor | Não implementado |
| Pacote Arch | Skeleton PKGBUILD |

## O que já está no repo

### Kernel (`kmod/`, API **3**)

| Sysfs | Acesso | Significado |
|-------|--------|-------------|
| `…/api_version` | RO | `3` |
| `…/thermal_profile` | RW | Nome do perfil |
| `…/fan_duty` | RO | Duty vivo CPU,GPU `%` |
| `…/fan_speed` | RW | Target `cpu,gpu` (`0,0` = auto) |
| `platform_profile` | RW | Classe ACPI |

Unload restaura fans a auto. `quiet` / `low-power` também forçam auto.

### Userspace (Meson)

```text
meson setup build && meson compile -C build
build/perfectsense [--json] status|mode|fan …
```

Biblioteca `libperfectsense` sobre sysfs; sem daemon. Aliases: `eco`, `turbo`, `bp`.

## Ainda não

RGB, attrs de bateria, D-Bus, FanAggressor, CPU power.
