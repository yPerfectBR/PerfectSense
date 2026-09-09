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

### Verificado na Fase 1 (leitura)

| Item | Valor |
|------|-------|
| DMI product name (observado) | `Predator PHN16-72` |
| GUID WMI gaming | `7A4DDFE7-5B5D-40B4-8595-4408E0CC7F56` |
| Method ID get misc | `23` |
| Índice misc: platform profile | `0x000B` |
| Empacotamento | índice bits 7:0; valor nos bits 15:8 do resultado; status bits 7:0 = 0 |

Códigos de perfil térmico → nomes PerfectSense:

| Código | Nome |
|--------|------|
| `0x06` | `low-power` (Eco) |
| `0x00` | `quiet` |
| `0x01` | `balanced` |
| `0x04` | `balanced-performance` |
| `0x05` | `performance` (Turbo) |

### Ainda a documentar

- Set misc (`22`) e restrições em AC
- Fans auto vs duty; RGB/logo; charge limit; tecla turbo/modo

## Docs relacionadas

- [Arquitetura](../architecture/overview.md)
