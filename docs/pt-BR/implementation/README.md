# Estado da implementação

> **Documente apenas o que existe de fato no repositório.**

## Estado atual

| Área | Estado |
|------|--------|
| Kernel | API **6** + hwmon `perfectsense` (temp/RPM) |
| CLI / lib | `temps`, `rgb effect`, `event inject`, `fan curve` |
| perfectsensed | Hotkey + AC demote + curva de fan opcional (SIGHUP) |
| Pacote Arch | Cutover (blacklist, enable, Conflicts) |

## Sensores

Sem nekro: hwmon `perfectsense` (`temp1/2`, `fan1/2`).

```text
perfectsense temps
perfectsense fan get   # inclui rpm
```

## Teclas

`event inject` / teclas físicas → política no daemon (uevent). Mode cicla
(salta rejeitados); turbo só em AC. **Sem D-Bus** — UI usa sysfs/CLI/input.
**AC offline** desce `quiet` / `balanced-performance` / `performance` →
`balanced` (restaura ao ligar se possível).

## Curva de fan

Default **off**. Pontos `temp:cpu%:gpu%`; o daemon aplica quando ligado.

```text
perfectsense fan curve status
perfectsense fan curve set cpu='(90,100,95),(85,89,90)' gpu='(90,100,90),(80,89,70)' interval_ms=1000
perfectsense fan curve on
```

Faixa = `(temp_min, temp_max, duty%)`. CPU e GPU são independentes.

Também: `fan get|set|auto` (nativo EC / manual).

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
