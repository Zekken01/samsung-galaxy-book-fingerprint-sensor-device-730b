#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
TOD_DIR="${REPO_ROOT}/libfprint-tod-canvasbio"
CONFIGURE_PAM=0

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--configure-pam]

Installs CanvasBio TOD driver and reloads fprintd services.

Options:
  --configure-pam   Enable fprintd via pam-auth-update after install
  -h, --help        Show this help
USAGE
}

log() { printf '[*] %s\n' "$*"; }
ok() { printf '[OK] %s\n' "$*"; }
warn() { printf '[!] %s\n' "$*"; }

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --configure-pam)
      CONFIGURE_PAM=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1
      ;;
  esac
  shift
done

require_cmd cargo
require_cmd meson
require_cmd ninja
require_cmd sudo
require_cmd udevadm

log "Building Rust CLI"
(
  cd "$REPO_ROOT"
  cargo build --release
)
ok "Rust CLI built"

log "Configuring TOD build"
(
  cd "$TOD_DIR"
  meson setup build --reconfigure
  ninja -C build
)
ok "TOD build completed"

log "Installing TOD driver and udev rule"
(
  cd "$TOD_DIR"
  sudo ninja -C build install
)
ok "TOD installed"

log "Reloading udev and restarting fprintd"
sudo udevadm control --reload-rules
sudo udevadm trigger
sudo systemctl restart fprintd || sudo pkill -9 fprintd || true
ok "Runtime services reloaded"

if [[ "$CONFIGURE_PAM" -eq 1 ]]; then
  if command -v pam-auth-update >/dev/null 2>&1; then
    log "Enabling fprintd PAM profile"
    sudo pam-auth-update --enable fprintd
    ok "PAM profile updated"
  else
    warn "pam-auth-update not found; skip PAM auto-configuration"
  fi
fi

cat <<NEXT

Installation finished.

Recommended next checks:
  1) lsusb -d 04e8:730b
  2) fprintd-list "\$USER"
  3) fprintd-enroll -f right-index-finger
  4) fprintd-verify -f right-index-finger
  5) Lock screen and test fingerprint unlock

NEXT
