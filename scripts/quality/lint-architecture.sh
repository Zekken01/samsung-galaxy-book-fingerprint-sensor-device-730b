#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

FAIL=0

report_fail() {
  printf '[FAIL] %s\n' "$*"
  FAIL=1
}

report_pass() {
  printf '[PASS] %s\n' "$*"
}

if rg -n 'crate::features::' src/domain >/tmp/canvasbio_lint_arch_domain_features.out; then
  report_fail "Domain layer must not depend on features layer"
  cat /tmp/canvasbio_lint_arch_domain_features.out
else
  report_pass "Domain layer has no features dependency"
fi

if rg -n 'crate::platform::' src/domain >/tmp/canvasbio_lint_arch_domain_platform.out; then
  report_fail "Domain layer must not depend on platform layer"
  cat /tmp/canvasbio_lint_arch_domain_platform.out
else
  report_pass "Domain layer has no platform dependency"
fi

if rg -n 'crate::features::|crate::platform::' src/shared >/tmp/canvasbio_lint_arch_shared_deps.out; then
  report_fail "Shared layer must not depend on features/platform layers"
  cat /tmp/canvasbio_lint_arch_shared_deps.out
else
  report_pass "Shared layer has no features/platform dependency"
fi

if rg -n 'crate::(sensor|usb|i18n|feedback|image)\b' src >/tmp/canvasbio_lint_arch_legacy_imports.out; then
  report_fail "Legacy top-level module imports detected"
  cat /tmp/canvasbio_lint_arch_legacy_imports.out
else
  report_pass "No legacy top-level module imports"
fi

rm -f \
  /tmp/canvasbio_lint_arch_domain_features.out \
  /tmp/canvasbio_lint_arch_domain_platform.out \
  /tmp/canvasbio_lint_arch_shared_deps.out \
  /tmp/canvasbio_lint_arch_legacy_imports.out

if [[ "$FAIL" -ne 0 ]]; then
  exit 1
fi

report_pass "Architecture dependency checks passed"
