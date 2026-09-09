# Estado da implementação

> **Documente apenas o que existe de fato no repositório.**
> Trabalho planejado: [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Estado atual

| Área | Estado |
|------|--------|
| Módulo kernel | **Fase 6:** API **5** (até RGB/logo) |
| libperfectsense | **Fase 6** |
| CLI | **Fase 6:** + `rgb` / `logo` / `lights` |
| perfectsensed / D-Bus | Não implementado |
| FanAggressor | Não implementado |
| Pacote Arch | Skeleton PKGBUILD |

## O que já está no repo

- `perfect_sense/`: perfil, fans, battery/misc, `lighting_reset`
- `four_zoned_kb/per_zone_mode` + `four_zone_mode`
- `back_logo/color`

```text
build/perfectsense rgb|logo|lights …
```

## Ainda não

D-Bus, FanAggressor, CPU power, tecla turbo/modo.
