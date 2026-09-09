# Estado da implementação

> **Documente apenas o que existe de fato no repositório.**
> Trabalho planejado: [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Estado atual

| Área | Estado |
|------|--------|
| Módulo kernel | **Fase 6:** API **5** |
| libperfectsense | **Fase 6+** |
| CLI | Sysfs (funciona **sem** daemon) |
| perfectsensed / D-Bus | **Fase 7:** `org.perfectsense.Sense1` |
| FanAggressor | Não implementado |
| Pacote Arch | Skeleton PKGBUILD |

## D-Bus

- Nome: `org.perfectsense.Sense1`
- Path: `/org/perfectsense/Sense1`
- Sinal: `ProfileChanged(s)` ao mudar perfil via D-Bus
- Unit: `perfectsensed.service`

```text
busctl introspect org.perfectsense.Sense1 /org/perfectsense/Sense1 org.perfectsense.Sense1
```

A CLI continua via sysfs sem o daemon.

## Ainda não

FanAggressor, CPU power, tecla turbo → D-Bus.
