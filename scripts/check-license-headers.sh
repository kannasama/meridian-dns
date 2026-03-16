#!/usr/bin/env bash
# scripts/check-license-headers.sh — Validate SPDX license headers on all source files.
# Exit 0 if all files have headers, exit 1 if any are missing.

set -euo pipefail

SPDX_ID="SPDX-License-Identifier: AGPL-3.0-or-later"
MISSING=0

check_files() {
  local pattern="$1"

  while IFS= read -r -d '' file; do
    if ! head -5 "$file" | grep -qF "$SPDX_ID"; then
      echo "MISSING: $file"
      MISSING=$((MISSING + 1))
    fi
  done < <(find . -path ./build -prune -o -path ./ui/node_modules -prune -o \
    -name "$pattern" -print0)
}

echo "Checking SPDX headers..."

# C++ source and headers
check_files "*.cpp"
check_files "*.hpp"

# TypeScript
check_files "*.ts"

# Vue SFCs
check_files "*.vue"

# SQL migrations
check_files "*.sql"

if [ "$MISSING" -gt 0 ]; then
  echo ""
  echo "ERROR: $MISSING file(s) missing SPDX license header."
  exit 1
else
  echo "OK: All source files have SPDX headers."
  exit 0
fi
