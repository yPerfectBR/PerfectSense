# Estado da implementação

> **Documente apenas o que existe de fato no repositório.**
> Trabalho planejado: [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Estado atual

| Área | Estado |
|------|--------|
| Módulo kernel | **Fase 1:** gate DMI, get WMI misc, sysfs `perfect_sense/` |
| libperfectsense | Diretório placeholder |
| CLI | Stub Fase 0 |
| perfectsensed / D-Bus | Não implementado |
| FanAggressor | Não implementado |
| Pacote Arch | Skeleton PKGBUILD |

## O que já está no repo

### Kernel (`kmod/`, API **1**)

Descarregar módulos WMI Acer gaming conflituosos antes de carregar. Paths:

| Sysfs | Acesso | Significado |
|-------|--------|-------------|
| `/sys/devices/platform/perfectsense/perfect_sense/api_version` | RO | Versão ABI (`1`) |
| `…/thermal_profile` | RO | Nome mapeado |
| `…/thermal_profile_raw` | RO | Código firmware |

### Outros

- Licenças AGPL/GPL conforme docs
- Documentação bilingue
- CLI Meson stub
- Placeholders de packaging

## Ainda não

Set de perfil, classe `platform_profile`, fans, RGB, D-Bus, etc. (fases 2+).
