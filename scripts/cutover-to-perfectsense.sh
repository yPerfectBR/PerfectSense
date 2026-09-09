#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Live cutover from nekro_sense / acer-wmi → PerfectSense (dev tree or installed).
# Prefer: packaging/arch + makepkg -si for a permanent cutover.
#
# Usage (from repo root, as root):
#   sudo ./scripts/cutover-to-perfectsense.sh
#   sudo PERFECTSENSE_SRC=/path/to/PerfectSense ./scripts/cutover-to-perfectsense.sh

set -euo pipefail

if [[ ${EUID} -ne 0 ]]; then
  echo "run as root (sudo)" >&2
  exit 1
fi

ROOT="${PERFECTSENSE_SRC:-}"
if [[ -z ${ROOT} ]]; then
  ROOT="$(cd "$(dirname "$0")/.." && pwd)"
fi
if [[ ! -f ${ROOT}/meson.build ]]; then
  echo "PerfectSense tree not found at ${ROOT}" >&2
  exit 1
fi

KO="${ROOT}/kmod/perfectsense.ko"
CLI="${ROOT}/build/perfectsense"
DAEMON="${ROOT}/build/perfectsensed"

echo "==> PerfectSense cutover (source tree: ${ROOT})"

if [[ ! -f ${KO} ]]; then
  echo "==> Building kmod (Arch stock gcc first; LLVM=1 only if needed)…"
  # Official Arch linux headers are gcc-built. LLVM=1 is a fallback for some
  # Arch-based derivatives with clang kernels — not the primary path.
  if ! make -C "${ROOT}/kmod"; then
    make -C "${ROOT}/kmod" LLVM=1
  fi
fi
if [[ ! -x ${CLI} || ! -x ${DAEMON} ]]; then
  echo "==> Building userspace…"
  meson setup "${ROOT}/build" --reconfigure >/dev/null 2>&1 || \
    meson setup "${ROOT}/build"
  meson compile -C "${ROOT}/build"
fi

install -Dm644 "${ROOT}/packaging/dbus/org.perfectsense.Sense1.conf" \
  /etc/dbus-1/system.d/org.perfectsense.Sense1.conf
install -Dm644 "${ROOT}/packaging/modprobe.d/perfectsense.conf" \
  /etc/modprobe.d/perfectsense-cutover.conf
install -Dm644 "${ROOT}/packaging/modules-load.d/perfectsense.conf" \
  /etc/modules-load.d/perfectsense-cutover.conf
install -Dm644 "${ROOT}/packaging/systemd/perfectsensed.service" \
  /usr/lib/systemd/system/perfectsensed.service
install -Dm644 "${ROOT}/packaging/systemd/perfectsensed.path" \
  /usr/lib/systemd/system/perfectsensed.path

# Prefer installed binaries if present; else use build/
if [[ ! -x /usr/bin/perfectsensed ]]; then
  install -Dm755 "${DAEMON}" /usr/local/bin/perfectsensed
  install -Dm755 "${CLI}" /usr/local/bin/perfectsense
  # Point unit at /usr/local if we installed there
  if ! grep -q '/usr/local/bin/perfectsensed' /usr/lib/systemd/system/perfectsensed.service; then
    sed -i 's|^ExecStart=.*|ExecStart=/usr/local/bin/perfectsensed|' \
      /usr/lib/systemd/system/perfectsensed.service
  fi
fi

echo "==> Stopping rivals…"
systemctl stop perfectsensed.service 2>/dev/null || true
pkill -x perfectsensed 2>/dev/null || true
modprobe -r nekro_sense 2>/dev/null || rmmod nekro_sense 2>/dev/null || true
modprobe -r acer-wmi 2>/dev/null || true
rmmod perfectsense 2>/dev/null || true

echo "==> Loading perfectsense.ko…"
if modprobe perfectsense 2>/dev/null; then
  :
elif insmod "${KO}"; then
  :
else
  echo "failed to load perfectsense.ko" >&2
  exit 1
fi

sleep 0.3
API=/sys/devices/platform/perfectsense/perfect_sense/api_version
if [[ ! -r ${API} ]]; then
  echo "sysfs missing after load" >&2
  exit 1
fi
echo "==> api_version=$(cat "${API}")"

systemctl daemon-reload
systemctl enable perfectsensed.path perfectsensed.service
systemctl start perfectsensed.path
systemctl restart perfectsensed.service

echo "==> status"
systemctl --no-pager --full status perfectsensed.service | head -15 || true
if command -v perfectsense >/dev/null; then
  perfectsense status | head -20
elif [[ -x ${CLI} ]]; then
  LD_LIBRARY_PATH="${ROOT}/build${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "${CLI}" status | head -20
fi

echo
echo "Cutover OK. Reboot recommended so blacklist + modules-load apply cleanly."
echo "Rollback: sudo ${ROOT}/scripts/rollback-to-nekro.sh"
