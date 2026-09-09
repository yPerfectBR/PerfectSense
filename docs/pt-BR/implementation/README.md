# Estado da implementação

> **Documente apenas o que existe de fato no repositório.**
> Trabalho planejado: [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Estado atual

| Área | Estado |
|------|--------|
| Módulo kernel | **Fase 2:** get/set perfil, `platform_profile`, gate AC |
| libperfectsense | Placeholder |
| CLI | Stub Fase 0 |
| perfectsensed / D-Bus | Não implementado |
| FanAggressor | Não implementado |
| Pacote Arch | Skeleton PKGBUILD |

## O que já está no repo

### Kernel (`kmod/`, API **2**)

| Sysfs | Acesso | Significado |
|-------|--------|-------------|
| `…/perfect_sense/api_version` | RO | `2` |
| `…/thermal_profile` | RW | Nome do perfil |
| `…/thermal_profile_raw` | RO | Código firmware |
| `…/ac_online` | RO | AC segundo firmware |
| `/sys/firmware/acpi/platform_profile` | RW | Classe `perfectsense` |

**Política AC:** em bateria, `quiet` / `balanced-performance` / `performance` →
`-EOPNOTSUPP`. `low-power` e `balanced` ok.

Fans ainda não (Fase 3).

## Ainda não

Fans, RGB, D-Bus, FanAggressor, CPU power (fases 3+).
