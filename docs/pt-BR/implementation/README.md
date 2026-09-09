# Estado da implementação

> **Documente apenas o que existe de fato no repositório.**

## Estado atual

| Área | Estado |
|------|--------|
| Kernel | API **6** + hwmon `perfectsense` (temp/RPM) |
| CLI / lib | `temps`, `rgb effect`, `event inject`, FA set |
| perfectsensed | Hotkey + debounce; FanAggressor |
| Pacote Arch | Cutover (blacklist, enable, Conflicts) |

## Sensores

Sem nekro: hwmon `perfectsense` (`temp1/2`, `fan1/2`).

```text
perfectsense temps
perfectsense fan get   # inclui rpm
```

## Teclas

`event inject` / teclas físicas → D-Bus `Hotkey`. Mode cicla (salta rejeitados);
turbo só em AC (toggle). **AC offline** desce `quiet` /
`balanced-performance` / `performance` → `balanced` (restaura ao ligar se
possível).

## Pacote Arch

Cutover completo: [cutover.md](../cutover.md).

```text
cd packaging/arch && PERFECTSENSE_SRC=../.. makepkg -si
```

Como no nekro: `nvidia-open` + `nvidia-powerd` para ~80 W→~140 W.

PerfectSense: **off** em `low-power`/`quiet`; **on** de `balanced` para cima.

```text
perfectsense gpu powerd get|on|off
```
