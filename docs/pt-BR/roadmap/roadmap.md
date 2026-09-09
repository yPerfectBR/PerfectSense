# Roteiro de implementação

> **Este documento é um plano.** Descreve fases pretendidas. **Não** é o
> changelog do que já funciona. O que realmente existe está em
> [implementation/](../implementation/README.md).

Hardware alvo: **Acer Predator Helios Neo 16 PHN16-72-74VX** em **Arch Linux**.

Cada fase termina com **testes manuais** na máquina. O implementador pedirá
comandos concretos após cada feature pequena.

## Fase 0 — Esqueleto do repositório

- Stub Meson (`libperfectsense`, `perfectsense`, `perfectsensed`)
- Stub DKMS do `perfectsense.ko`
- Skeleton `packaging/arch/`
- Docs `docs/en` + `docs/pt-BR`
- Notas de blacklist de módulos WMI conflituosos

**Saída:** stubs compilam; docs ok; AGPL-3.0.

## Fase 1 — Probe kernel + API + get de perfil

- Gate DMI PHN16-72-74VX
- Bind/probe WMI
- Sysfs `perfect_sense/api_version`
- Leitura do perfil térmico (só get)

## Fase 2 — Set de perfil + `platform_profile`

- Set via WMI
- Choices/mapeamento `platform_profile`
- Falha segura em bateria para modos agressivos (como o firmware exige)

## Fase 3 — Fans get / set / auto

- Duty ao vivo get + set
- Modo auto
- Formato sysfs claro (`cpu,gpu`)

## Fase 4 — Biblioteca + CLI

- `libperfectsense`
- CLI texto humano + **`--json`**
- Sem daemon para esses comandos

## Fase 5 — Bateria e attrs misc

- Limiter / health conforme suportado
- USB charging, LCD override, backlight timeout, boot sound

## Fase 6 — RGB teclado + logo

- Quatro zonas KB
- Logo / lightbar
- Helpers off/reset

## Fase 7 — `perfectsensed` + D-Bus (sem FanAggressor)

- Unit **system** (root)
- `org.perfectsense.Sense1`
- CLI continua sem daemon

## Fase 8 — Controles de CPU

- Governor, turbo, EPP, RAPL, freq máx.
- Presets opcionais
- Sem loop térmico

## Fase 9 — FanAggressor (offset)

- Liga/desliga sob demanda
- Timer **só enquanto ativo**
- Histerese; offsets por eixo
- Off → auto

## Fase 10 — Eventos de tecla + polish

- Uevent/input → D-Bus
- Man pages, PKGBUILD, hardening

## Fora de escopo

- GUI neste repositório
- Pacotes CachyOS
- Alterar o stack Pandora
- Garantia de outros modelos (só ganchos limpos)

## Porquê esta ordem

Perfil + fans validam WMI cedo. Attrs/RGB vêm depois. D-Bus após CLI estável.
CPU power (só userspace) antes do FanAggressor. Offset por último: único trabalho
periódico intencional.
