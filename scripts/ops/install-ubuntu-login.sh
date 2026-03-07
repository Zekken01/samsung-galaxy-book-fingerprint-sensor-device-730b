#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
TOD_DIR="${REPO_ROOT}/libfprint-tod-canvasbio"
CONFIGURE_PAM=0
CONFIGURE_PAM_FORCE=0
DETECT_PROBE_MS=170
DETECT_RETRY_MS=30
SYSTEMD_DROPIN_DIR="/etc/systemd/system/fprintd.service.d"
SYSTEMD_DROPIN_FILE="${SYSTEMD_DROPIN_DIR}/canvasbio-tuning.conf"

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--configure-pam] [--configure-pam-force]

Installs CanvasBio TOD driver and reloads fprintd services.

Options:
  --configure-pam         Enable fprintd via pam-auth-update after install
  --configure-pam-force   Force non-interactive PAM update (uses --force)
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

apply_persistent_tuning_profile() {
  log "Applying persistent fprintd tuning profile"
  sudo mkdir -p "$SYSTEMD_DROPIN_DIR"
  sudo tee "$SYSTEMD_DROPIN_FILE" >/dev/null <<EOF
[Service]
Environment=CANVASBIO_DETECT_PROBE_MS=${DETECT_PROBE_MS}
Environment=CANVASBIO_DETECT_RETRY_MS=${DETECT_RETRY_MS}
EOF
  sudo systemctl daemon-reload
  ok "Persistent tuning applied at ${SYSTEMD_DROPIN_FILE}"
}

configure_pam_profile() {
  if ! command -v pam-auth-update >/dev/null 2>&1; then
    warn "pam-auth-update not found; skip PAM auto-configuration"
    return 0
  fi

  log "Enabling fprintd PAM profile"
  if [[ "$CONFIGURE_PAM_FORCE" -eq 1 ]]; then
    if sudo DEBIAN_FRONTEND=noninteractive pam-auth-update --enable fprintd --force; then
      ok "PAM profile updated (forced non-interactive)"
      return 0
    fi
    warn "Forced PAM update failed"
    return 1
  fi

  if [[ -t 0 ]] && [[ -t 1 ]]; then
    if sudo pam-auth-update --enable fprintd; then
      ok "PAM profile updated"
      return 0
    fi
    warn "Interactive PAM update failed"
    return 1
  fi

  warn "Non-interactive environment detected; retry with --configure-pam-force"
  return 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --configure-pam)
      CONFIGURE_PAM=1
      ;;
    --configure-pam-force)
      CONFIGURE_PAM=1
      CONFIGURE_PAM_FORCE=1
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

apply_persistent_tuning_profile

log "Reloading udev and restarting fprintd"
sudo udevadm control --reload-rules
sudo udevadm trigger
sudo systemctl restart fprintd || sudo pkill -9 fprintd || true
ok "Runtime services reloaded"

if [[ "$CONFIGURE_PAM" -eq 1 ]]; then
  configure_pam_profile || true
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
