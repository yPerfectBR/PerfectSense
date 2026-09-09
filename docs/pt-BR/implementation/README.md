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

## NVIDIA PowerMizer

`mode set` / tecla de modos também pedem Preferred Mode:

| Perfil | PowerMizer |
|--------|------------|
| low-power / quiet | Normal |
| balanced | Auto |
| bp / performance | Prefer Maximum |

`perfectsense gpu powermizer get|set …`

Em Wayland puro o NV-CONTROL pode não aplicar (fica em Normal).
