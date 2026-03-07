#!/usr/bin/env bash
set -euo pipefail

VENDOR_ID="04e8"
PRODUCT_ID="730b"
DRIVER_PATTERN="*tod-canvasbio*.so"
USER_NAME="${SUDO_USER:-${USER:-}}"

usage() {
  cat <<USAGE
Usage: $(basename "$0") [--user <username>] [--finger <finger-name>] [--ci]

Checks whether CanvasBio TOD login stack is correctly installed.

Options:
  --user <username>   User for fprintd-list check (default: current user)
  --finger <name>     Also run fprintd-verify for this finger
  --ci                Non-strict mode for CI/sandbox (downgrade runtime checks to WARN)
  -h, --help          Show this help
USAGE
}

PASS=0
FAIL=0
WARN=0
VERIFY_FINGER=""
CI_MODE=0

pass() { printf '[PASS] %s\n' "$*"; PASS=$((PASS + 1)); }
fail() { printf '[FAIL] %s\n' "$*"; FAIL=$((FAIL + 1)); }
warn() { printf '[WARN] %s\n' "$*"; WARN=$((WARN + 1)); }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --user)
      USER_NAME="${2:-}"
      shift
      ;;
    --finger)
      VERIFY_FINGER="${2:-}"
      shift
      ;;
    --ci)
      CI_MODE=1
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

if [[ -z "$USER_NAME" ]]; then
  echo "Unable to determine user. Set --user explicitly." >&2
  exit 1
fi

if lsusb -d "${VENDOR_ID}:${PRODUCT_ID}" >/dev/null 2>&1; then
  pass "USB device ${VENDOR_ID}:${PRODUCT_ID} detected"
else
  if [[ "$CI_MODE" -eq 1 ]]; then
    warn "USB device ${VENDOR_ID}:${PRODUCT_ID} not detected (CI mode)"
  else
    fail "USB device ${VENDOR_ID}:${PRODUCT_ID} not detected"
  fi
fi

if command -v pkg-config >/dev/null 2>&1; then
  if TOD_DIR="$(pkg-config --variable=tod_driversdir libfprint-2-tod-1 2>/dev/null)"; then
    if compgen -G "${TOD_DIR}/${DRIVER_PATTERN}" >/dev/null; then
      pass "TOD module installed in ${TOD_DIR}"
    else
      if [[ "$CI_MODE" -eq 1 ]]; then
        warn "TOD module ${DRIVER_PATTERN} not found in ${TOD_DIR} (CI mode)"
      else
        fail "TOD module ${DRIVER_PATTERN} not found in ${TOD_DIR}"
      fi
    fi
  else
    warn "Could not resolve tod_driversdir via pkg-config"
  fi
else
  warn "pkg-config not found; skipped TOD path validation"
fi

if [[ -f /usr/lib/udev/rules.d/60-libfprint-canvasbio.rules ]]; then
  pass "udev rule installed"
else
  warn "udev rule not found at /usr/lib/udev/rules.d/60-libfprint-canvasbio.rules"
fi

if systemctl is-active --quiet fprintd >/dev/null 2>&1; then
  pass "fprintd service active"
else
  warn "fprintd service is not active or systemd is unavailable"
fi

if fprintd-list "$USER_NAME" >/tmp/canvasbio_fprintd_list.out 2>/tmp/canvasbio_fprintd_list.err; then
  pass "fprintd-list succeeded for user ${USER_NAME}"
else
  if [[ "$CI_MODE" -eq 1 ]]; then
    warn "fprintd-list failed for user ${USER_NAME} (CI mode)"
  else
    fail "fprintd-list failed for user ${USER_NAME}"
    cat /tmp/canvasbio_fprintd_list.err >&2 || true
  fi
fi
rm -f /tmp/canvasbio_fprintd_list.out /tmp/canvasbio_fprintd_list.err

if [[ -n "$VERIFY_FINGER" ]]; then
  if fprintd-verify -f "$VERIFY_FINGER" >/tmp/canvasbio_fprintd_verify.out 2>/tmp/canvasbio_fprintd_verify.err; then
    pass "fprintd-verify succeeded for finger ${VERIFY_FINGER}"
  else
    if [[ "$CI_MODE" -eq 1 ]]; then
      warn "fprintd-verify failed for finger ${VERIFY_FINGER} (CI mode)"
    else
      fail "fprintd-verify failed for finger ${VERIFY_FINGER}"
      cat /tmp/canvasbio_fprintd_verify.err >&2 || true
    fi
  fi
  rm -f /tmp/canvasbio_fprintd_verify.out /tmp/canvasbio_fprintd_verify.err
fi

echo
echo "Summary: PASS=${PASS} WARN=${WARN} FAIL=${FAIL}"
if [[ "$FAIL" -gt 0 ]]; then
  exit 1
fi
