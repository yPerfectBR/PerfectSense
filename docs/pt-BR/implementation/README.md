# Estado da implementação

> **Documente apenas o que existe de fato no repositório.**
> Trabalho planejado: [../roadmap/roadmap.md](../roadmap/roadmap.md).

## Estado atual

| Área | Estado |
|------|--------|
| Módulo kernel | Stub Fase 0 (`kmod/`) — só mensagem, sem WMI |
| libperfectsense | Diretório placeholder |
| CLI | Stub Fase 0 (`perfectsense`) |
| perfectsensed / D-Bus | Não implementado |
| FanAggressor | Não implementado |
| Pacote Arch | Skeleton PKGBUILD (não instalável) |

## O que já está no repo

- Licença AGPL-3.0 (userspace/docs); `kmod/` GPL-2.0-or-later
- Docs de design, roteiro e RE (`docs/en`, `docs/pt-BR`)
- Projeto Meson com CLI stub
- Placeholders DKMS/modprobe

Conforme as features entrarem, esta página listará attrs sysfs, subcomandos CLI
e interfaces D-Bus com versão — sem afirmar nada que não seja compilável/testável
no PHN16-72-74VX.
