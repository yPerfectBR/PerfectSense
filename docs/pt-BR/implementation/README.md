# Estado da implementação

> **Documente apenas o que existe de fato no repositório.**

## Estado atual

| Área | Estado |
|------|--------|
| Kernel | API **5** |
| CLI / lib | até Fase 9 |
| perfectsensed | FanAggressor (timerfd só se ligado) |
| Pacote Arch | Skeleton |

## FanAggressor

```text
perfectsense fan aggressor on|off|status
```

Desligado por padrão (sem timer). Config: `/run/perfectsense/fan-aggressor.conf`.

## Ainda não

Tecla turbo → D-Bus, man pages, PKGBUILD instalável.
