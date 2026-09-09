#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Undo a live/dev cutover and restore nekro_sense for day-to-day.
#
# Usage:
#   sudo ./scripts/rollback-to-nekro.sh

set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
  echo "run as root (sudo)" >&2
  exit 1
fi

echo "==> PerfectSense → nekro rollback"

systemctl disable --now perfectsensed.service perfectsensed.path 2>/dev/null || true
pkill -x perfectsensed 2>/dev/null || true
modprobe -r perfectsense 2>/dev/null || rmmod perfectsense 2>/dev/null || true

# Remove cutover-only drop-ins (package-managed files under /usr/lib stay until uninstall)
rm -f /etc/modprobe.d/perfectsense-cutover.conf
rm -f /etc/modules-load.d/perfectsense-cutover.conf

# If the Arch package is installed, warn — blacklist still applies from /usr/lib
if pacman -Q perfectsense >/dev/null 2>&1; then
  echo "==> NOTE: package 'perfectsense' is still installed."
  echo "    Blacklist in /usr/lib/modprobe.d/perfectsense.conf still blocks nekro."
  echo "    Fully rollback with: sudo pacman -Rns perfectsense"
fi

if modprobe nekro_sense 2>/dev/null; then
  echo "==> nekro_sense loaded"
  lsmod | grep -E 'nekro|perfect' || true
else
  echo "==> could not modprobe nekro_sense (blacklisted or missing module?)" >&2
  echo "    Try: sudo pacman -Rns perfectsense && sudo modprobe nekro_sense" >&2
  exit 1
fi

echo "Rollback OK."
