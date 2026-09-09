# Estado da implementação

> **Documente apenas o que existe de fato no repositório.**
> Trabalho planejado: [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Estado atual

| Área | Estado |
|------|--------|
| Módulo kernel | **Fase 6:** API **5** |
| libperfectsense | **Fase 8:** + CPU power |
| CLI | + `cpu` presets |
| perfectsensed / D-Bus | **Fase 7+8** |
| FanAggressor | Não implementado |
| Pacote Arch | Skeleton |

## CPU power (Fase 8)

```text
perfectsense cpu status|snapshot|preset|restore|set …
```

Presets: `power`, `balanced`, `performance`, `turbo` (+ `--sync-profile`).
Snapshot em `/run/perfectsense/cpu-power.snap`.

## Ainda não

FanAggressor, tecla turbo → D-Bus, polish de pacote.
