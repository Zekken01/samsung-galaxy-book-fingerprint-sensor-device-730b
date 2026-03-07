#!/usr/bin/env bash
set -euo pipefail

DRIVER_PATTERN="*tod-canvasbio*.so"
UDEV_RULE="/usr/lib/udev/rules.d/60-libfprint-canvasbio.rules"
SYSTEMD_DROPIN_FILE="/etc/systemd/system/fprintd.service.d/canvasbio-tuning.conf"

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--restore-common-auth <backup-path>]

Uninstalls CanvasBio TOD artifacts and optionally restores PAM common-auth.

Options:
  --restore-common-auth <path>  Restore /etc/pam.d/common-auth from backup
  -h, --help                    Show help
USAGE
}

RESTORE_COMMON_AUTH=""

log() { printf '[*] %s\n' "$*"; }
ok() { printf '[OK] %s\n' "$*"; }
warn() { printf '[!] %s\n' "$*"; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --restore-common-auth)
      RESTORE_COMMON_AUTH="${2:-}"
      shift
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

if command -v pkg-config >/dev/null 2>&1; then
  TOD_DIR="$(pkg-config --variable=tod_driversdir libfprint-2-tod-1 2>/dev/null || true)"
else
  TOD_DIR=""
fi

if [[ -n "$TOD_DIR" ]]; then
  for path in "$TOD_DIR"/${DRIVER_PATTERN}; do
    if [[ -e "$path" ]]; then
      log "Removing TOD module: $path"
      sudo rm -f "$path"
      ok "Removed $path"
    fi
  done
else
  warn "Could not resolve tod_driversdir; skipped TOD module removal"
fi

if [[ -f "$UDEV_RULE" ]]; then
  log "Removing udev rule: $UDEV_RULE"
  sudo rm -f "$UDEV_RULE"
  ok "Removed udev rule"
else
  warn "udev rule not present: $UDEV_RULE"
fi

if [[ -f "$SYSTEMD_DROPIN_FILE" ]]; then
  log "Removing fprintd tuning profile: $SYSTEMD_DROPIN_FILE"
  sudo rm -f "$SYSTEMD_DROPIN_FILE"
  ok "Removed tuning profile"
else
  warn "fprintd tuning profile not present: $SYSTEMD_DROPIN_FILE"
fi

if [[ -n "$RESTORE_COMMON_AUTH" ]]; then
  if [[ -f "$RESTORE_COMMON_AUTH" ]]; then
    log "Restoring /etc/pam.d/common-auth from backup"
    sudo cp "$RESTORE_COMMON_AUTH" /etc/pam.d/common-auth
    ok "common-auth restored"
  else
    warn "Backup path not found: $RESTORE_COMMON_AUTH"
  fi
fi

log "Reloading runtime services"
sudo systemctl daemon-reload || true
sudo udevadm control --reload-rules
sudo udevadm trigger
sudo systemctl restart fprintd || sudo pkill -9 fprintd || true
ok "Rollback completed"
