#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

REINSTALL=0
CONFIGURE_PAM=0
VERIFY_FINGER=""

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--reinstall] [--configure-pam] [--verify-finger <name>]

Attempts to repair common Ubuntu fingerprint stack failures for CanvasBio.

Options:
  --reinstall             Run full install workflow before service recovery
  --configure-pam         Re-apply pam-auth-update --enable fprintd
  --verify-finger <name>  Run post-repair verify check for finger
  -h, --help              Show this help
USAGE
}

log() { printf '[*] %s\n' "$*"; }
ok() { printf '[OK] %s\n' "$*"; }
warn() { printf '[!] %s\n' "$*"; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --reinstall)
      REINSTALL=1
      ;;
    --configure-pam)
      CONFIGURE_PAM=1
      ;;
    --verify-finger)
      VERIFY_FINGER="${2:-}"
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

if [[ "$REINSTALL" -eq 1 ]]; then
  log "Running install workflow"
  if [[ "$CONFIGURE_PAM" -eq 1 ]]; then
    "${REPO_ROOT}/scripts/ops/install-ubuntu-login.sh" --configure-pam
  else
    "${REPO_ROOT}/scripts/ops/install-ubuntu-login.sh"
  fi
else
  log "Reloading udev + restarting fprintd"
  sudo udevadm control --reload-rules
  sudo udevadm trigger
  sudo systemctl restart fprintd || sudo pkill -9 fprintd || true
  ok "Runtime services refreshed"

  if [[ "$CONFIGURE_PAM" -eq 1 ]]; then
    if command -v pam-auth-update >/dev/null 2>&1; then
      log "Re-applying PAM profile for fprintd"
      sudo pam-auth-update --enable fprintd
      ok "PAM updated"
    else
      warn "pam-auth-update not found; skipped"
    fi
  fi
fi

USER_NAME="${SUDO_USER:-${USER:-}}"
if [[ -z "$USER_NAME" ]]; then
  USER_NAME="$(id -un)"
fi

log "Post-repair healthcheck"
"${REPO_ROOT}/scripts/ops/healthcheck-ubuntu-login.sh" --user "$USER_NAME"

if [[ -n "$VERIFY_FINGER" ]]; then
  log "Interactive verification check"
  fprintd-verify -f "$VERIFY_FINGER"
fi

ok "Repair workflow finished"
