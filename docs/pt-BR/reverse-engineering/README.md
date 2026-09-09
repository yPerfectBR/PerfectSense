# Engenharia reversa

Esta secção regista **métodos** e **achados** sobre as interfaces de firmware
usadas pelo PerfectSense. Não é roteiro de produto
([roadmap](../roadmap/roadmap.md)) nem API shipada
([implementation](../implementation/README.md)).

## Metodologia (visão geral)

1. **Identidade** — Confirmar DMI (`PHN16-72` / variante) e versão de BIOS.
2. **Inventário ACPI** — Dump e desmontagem (`acpidump` → `acpixtract` →
   `iasl -d`). Localizar GUIDs WMI e dispatchers ASL.
3. **Mapa de métodos** — IDs numéricos e tamanhos de buffer (fans, perfil,
   RGB, bateria, etc.).
4. **Validação por traces** — Quando possível, comparar layouts com traces WMI
   de um SO de referência.
5. **Getters conservadores** — Implementar e testar **leitura** antes de
   **escrita**. Registar erros de firmware; não adivinhar tamanhos.
6. **Writes com gate** — Só após allowlist DMI, checks de tamanho e payloads
   conhecidos. Preferir restaurar fans a auto em falha/unload.
7. **Cruzar sensores** — Correlacionar controlos com hwmon (temp/RPM).

## Regras de segurança

- Tratar todo WMI set como potencialmente persistente até prova em contrário.
- Não stressar thermal/battery health em bateria sem plano de recuperação.
- Probes de debug fora do caminho de produção.

## Traces e artefactos

Traces brutos (quando existirem) em [`traces/`](traces/). Descrever captura
junto a cada ficheiro (ferramenta, SO, BIOS, ação que gerou o tráfego).

## Notas de firmware (classe PHN16-72)

### Verificado nas Fases 1–2

| Item | Valor |
|------|-------|
| DMI product name | `Predator PHN16-72` |
| GUID WMI gaming | `7A4DDFE7-5B5D-40B4-8595-4408E0CC7F56` |
| Get misc | `23` |
| Set misc | `22` |
| Get sys info | `5` |
| Cmd AC/bateria | `0x02` (≠0 ⇒ AC) |
| Índice perfis suportados | `0x000A` |
| Índice platform profile | `0x000B` |

Em bateria, firmware/OEM bloqueia `quiet`, `balanced-performance` e `performance`.

### Fans (Fase 3)

| Item | Valor |
|------|-------|
| Behaviour | method `14` |
| Set speed | method `16` |
| Get duty vivo | method `17` |
| IDs | CPU `0x01`, GPU `0x04` |
| Auto | `0x410009` |
| Max | `0x820009` |

`fan_duty` = leitura viva; `fan_speed` = último target (`0,0` = auto).

### Battery / misc (Fase 5)

| Item | Valor |
|------|-------|
| Battery health | GUID5 methods `20`/`21` |
| Limiter / calibração | modes `1` / `2` |
| Boot sound | misc `0x06` |
| USB charging / KB timeout | GUID3 ApgeAction |
| LCD override | GUID4 gaming profile methods `3`/`1` |

Neste PHN16-72, LCD e KB timeout leem `-1` por enquanto.

### RGB + logo (Fase 6)

| Item | Valor |
|------|-------|
| Per-zone RGB | methods `6`/`7`, zones `1/2/4/8` |
| KB backlight | methods `20`/`21` |
| Logo | methods `12`/`13` |
| Sysfs | `four_zoned_kb/*`, `back_logo/color` |

### Teclas / AC (Fase 10)

| Item | Valor |
|------|-------|
| GUID de eventos | `676AA15E-6A47-4D9F-A2CC-1E6D18D14026` |
| Função turbo/modo | `0x07` |
| Função AC | `0x08` |
| Turbo / modo (PHN16-72) | `key_num` `0x4` / `0x5` |
| AC off/on | `key_num` `0` / `1` |

Kernel → input + uevent; só um driver pode instalar o notify neste GUID.

## Docs relacionadas

- [Arquitetura](../architecture/overview.md)
