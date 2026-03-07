#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

git config core.hooksPath .githooks

echo "[OK] Git hooks installed (core.hooksPath=.githooks)"
echo "[OK] pre-commit now runs ./scripts/run-quality-gates.sh"
