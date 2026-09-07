#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEFAULT_BASELINE_ROOT="${REPO_ROOT}/.a0-baseline"
for candidate in \
  "${REPO_ROOT}/.a0-baseline" \
  "${REPO_ROOT}/../.a0-baseline" \
  "${REPO_ROOT}/../../.a0-baseline"; do
  if [[ -d "${candidate}/install/lib" ]]; then
    DEFAULT_BASELINE_ROOT="$candidate"
    break
  fi
done
A0_BASELINE_ROOT="${A0_BASELINE_ROOT:-${A0_WORK_DIR:-${DEFAULT_BASELINE_ROOT}}}"
A0_BASELINE_PREFIX="${A0_BASELINE_PREFIX:-${A0_BASELINE_ROOT}/install}"
A0_C1_OVERLAY_PREFIX="${A0_C1_OVERLAY_PREFIX:-${A0_PREFIX:-${A0_BASELINE_ROOT}/install-c1-overlay}}"

missing=0
need_dir() {
  local p="$1"
  if [[ ! -d "$p" ]]; then
    echo "MISSING: $p"
    missing=1
  else
    echo "OK: $p"
  fi
}

need_cmd() {
  local c="$1"
  if ! command -v "$c" >/dev/null 2>&1; then
    echo "MISSING TOOL: $c"
    missing=1
  else
    echo "OK TOOL: $c"
  fi
}

need_cmd_optional() {
  local c="$1"
  if ! command -v "$c" >/dev/null 2>&1; then
    echo "OPTIONAL TOOL MISSING: $c (install/launch helpers require it)"
  else
    echo "OK TOOL: $c"
  fi
}

echo "== Sudoku Android runtime setup check =="
need_cmd meson
need_cmd ninja
need_cmd pkg-config
need_cmd_optional adb
need_cmd glib-compile-schemas

need_dir "$A0_BASELINE_PREFIX/lib"
need_dir "$A0_C1_OVERLAY_PREFIX/lib"
need_dir "$A0_C1_OVERLAY_PREFIX/share/glib-2.0/schemas"

mkdir -p "$A0_C1_OVERLAY_PREFIX/share/python-stdlib"

if [[ "$missing" -ne 0 ]]; then
  echo
  echo "Runtime baseline is incomplete."
  echo "Expected baseline root: $A0_BASELINE_ROOT"
  echo "Then run: scripts/android/install-python-runtime.sh beeware-3.9"
  exit 1
fi

echo "Runtime baseline looks usable."
echo "Next: scripts/android/install-python-runtime.sh beeware-3.9"
