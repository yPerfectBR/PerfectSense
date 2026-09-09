# Cutover: nekro → PerfectSense

**Cutover** = passar o dia a dia para o PerfectSense como **único** dono do WMI
Acer gaming: blacklist/unload de rivais (`nekro_sense`, `acer-wmi`), carregar
`perfectsense.ko` no boot e correr `perfectsensed`.

Alvo de packaging: **Arch Linux** puro (`linux` / `linux-headers`, `nvidia-open`,
`nvidia-utils`). Derivados Arch podem reaproveitar o PKGBUILD, mas não são o foco.

## Pacote Arch (recomendado)

```bash
cd packaging/arch
PERFECTSENSE_SRC=../.. makepkg -si
```

O `post_install` faz DKMS, descarrega nekro se estiver carregado, `modprobe
perfectsense` e `systemctl enable` de `.path` + `.service`.

Verificar:

```bash
lsmod | grep -E 'perfect|nekro'
systemctl status perfectsensed --no-pager | head
/usr/bin/perfectsense status
```

Se `perfectsense` no `PATH` ainda for um shim do Pandora/`~/.local/bin`, use
`/usr/bin/perfectsense` (ou remova/renomeie o shim local).

## Cutover rápido a partir do git

```bash
sudo ./scripts/cutover-to-perfectsense.sh
# voltar:
sudo ./scripts/rollback-to-nekro.sh
```

Com o pacote instalado, rollback completo: `sudo pacman -Rns perfectsense`.

## GPU

`nvidia-open` + `systemctl enable nvidia-powerd` (como no nekro). O PerfectSense
só faz start/stop conforme o perfil térmico.
