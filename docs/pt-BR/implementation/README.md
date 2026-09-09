# Estado da implementação

> **Documente apenas o que existe de fato no repositório.**

## Estado atual

| Área | Estado |
|------|--------|
| Kernel | API **6** (teclas/AC → input + uevent) |
| CLI / lib | até Fase 9+ |
| perfectsensed | `Hotkey` + FanAggressor |
| Man pages | `perfectsense(1)`, `perfectsensed(8)` |
| Pacote Arch | `packaging/arch/PKGBUILD` instalável |

## Teclas (Fase 10)

Módulo: GUID de notify WMI + `PerfectSense Hotkeys` (`KEY_PROG1`/`KEY_PROG2`).

Daemon: sinal D-Bus `Hotkey(s,u)`; **mode** cicla perfis (salta rejeitados em
bateria); **turbo** (só AC) alterna preset CPU `turbo` / restore.

Teste sintético: escrever em `…/perfect_sense/inject_event`.

## FanAggressor

```text
perfectsense fan aggressor on|off|status
```

Desligado por padrão (sem timer). Config: `/run/perfectsense/fan-aggressor.conf`.

## Pacote

```text
cd packaging/arch && PERFECTSENSE_SRC=../.. makepkg -si
```
