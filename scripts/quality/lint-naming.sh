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

while IFS= read -r path; do
  base="$(basename "$path")"
  case "$base" in
    lib.rs|main.rs|mod.rs) continue ;;
  esac
  if [[ ! "$base" =~ ^[a-z0-9_]+\.rs$ ]]; then
    report_fail "Rust file must be snake_case: $path"
  fi
done < <(find src -type f -name '*.rs' | sort)

while IFS= read -r path; do
  base="$(basename "$path")"
  if [[ ! "$base" =~ ^[a-z0-9-]+\.md$ ]]; then
    report_fail "Markdown file must be kebab-case: $path"
  fi
done < <(find docs -type f -name '*.md' | sort)

while IFS= read -r path; do
  base="$(basename "$path")"
  if [[ ! "$base" =~ ^[a-z0-9-]+\.sh$ ]]; then
    report_fail "Shell script must be kebab-case: $path"
  fi
done < <(find scripts -type f -name '*.sh' | sort)

if rg -n --glob '*.rs' '\bfn\s+[a-z0-9_]*[A-Z][A-Za-z0-9_]*\s*\(' src >/tmp/canvasbio_lint_naming_fn.out; then
  report_fail "Found non-snake_case Rust function names"
  cat /tmp/canvasbio_lint_naming_fn.out
else
  report_pass "Rust function names are snake_case"
fi

if rg -n --glob '*.rs' '\blet\s+(mut\s+)?[a-z][a-z0-9_]*[A-Z][A-Za-z0-9_]*\b' src >/tmp/canvasbio_lint_naming_var.out; then
  report_fail "Found non-snake_case Rust local variable names"
  cat /tmp/canvasbio_lint_naming_var.out
else
  report_pass "Rust local variable names are snake_case"
fi

rm -f /tmp/canvasbio_lint_naming_fn.out /tmp/canvasbio_lint_naming_var.out

if [[ "$FAIL" -ne 0 ]]; then
  exit 1
fi

report_pass "Naming conventions check passed"
