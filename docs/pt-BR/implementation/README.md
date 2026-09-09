# Estado da implementação

> **Documente apenas o que existe de fato no repositório.**

## Estado atual

| Área | Estado |
|------|--------|
| Kernel | API **6** + hwmon `perfectsense` (temp/RPM) |
| CLI / lib | `temps`, `rgb effect`, `event inject`, FA set |
| perfectsensed | Hotkey + debounce; FanAggressor |
| Pacote | blacklist + modules-load + path unit |

## Sensores

Sem nekro: hwmon `perfectsense` (`temp1/2`, `fan1/2`).

```text
perfectsense temps
perfectsense fan get   # inclui rpm
```

## Teclas

`event inject` / teclas físicas → D-Bus `Hotkey`. Mode cicla (salta rejeitados);
turbo só em AC (toggle).

## GPU (nvidia-powerd)

Como no nekro: `nvidia-open` + `nvidia-powerd` para ~80 W→~140 W.

PerfectSense: **off** em `low-power`/`quiet`; **on** de `balanced` para cima.

```text
perfectsense gpu powerd get|on|off
```
