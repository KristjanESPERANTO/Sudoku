#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CACHE_DIR="$REPO_ROOT/.cache/android-python-runtime"
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
A0_C1_OVERLAY_PREFIX="${A0_C1_OVERLAY_PREFIX:-${A0_PREFIX:-${A0_BASELINE_ROOT}/install-c1-overlay}}"
OVERLAY_LIB_DIR="$A0_C1_OVERLAY_PREFIX/lib"
OVERLAY_STDLIB_DIR="$A0_C1_OVERLAY_PREFIX/share/python-stdlib"

RUNTIME="${1:-beeware-3.9}"

BEEWARE_URL="https://github.com/beeware/Python-Android-support/releases/download/3.9-b3/Python-3.9-Android-support.b3.zip"
BEEWARE_ARCHIVE="$CACHE_DIR/Python-Android-support.b3.zip"
BEEWARE_EXTRACT_DIR="$CACHE_DIR/Python-Android-support.b3"

TERMUX_PYTHON_URL="https://packages.termux.dev/apt/termux-main/pool/main/p/python/python_3.13.13-1_x86_64.deb"
TERMUX_ANDROID_SUPPORT_URL="https://packages.termux.dev/apt/termux-main/pool/main/liba/libandroid-support/libandroid-support_29-1_x86_64.deb"
TERMUX_WORK_DIR="$CACHE_DIR/termux-3.13"
TERMUX_PYTHON_DEB="$TERMUX_WORK_DIR/python_3.13.13-1_x86_64.deb"
TERMUX_ANDROID_SUPPORT_DEB="$TERMUX_WORK_DIR/libandroid-support_29-1_x86_64.deb"

usage() {
  echo "Usage: $0 [beeware-3.9|termux-3.13]"
}

download_if_missing() {
  local url="$1"
  local dst="$2"
  if [[ ! -f "$dst" ]]; then
    echo "Downloading $(basename "$dst")..."
    curl -L -o "$dst" "$url"
  fi
}

clear_staged_runtime_libs() {
  rm -f "$OVERLAY_LIB_DIR"/libpython*.so* \
        "$OVERLAY_LIB_DIR"/libandroid-support.so* \
        "$OVERLAY_LIB_DIR"/libffi.so* \
        "$OVERLAY_LIB_DIR"/libsqlite3.so* \
        "$OVERLAY_LIB_DIR"/libbz2.so* \
        "$OVERLAY_LIB_DIR"/liblzma.so* \
        "$OVERLAY_LIB_DIR"/libssl*.so* \
        "$OVERLAY_LIB_DIR"/libcrypto*.so* \
        "$OVERLAY_LIB_DIR"/librubicon.so*
}

stage_beeware_39() {
  mkdir -p "$CACHE_DIR"
  download_if_missing "$BEEWARE_URL" "$BEEWARE_ARCHIVE"

  if [[ ! -d "$BEEWARE_EXTRACT_DIR" ]]; then
    rm -rf "$BEEWARE_EXTRACT_DIR"
    mkdir -p "$BEEWARE_EXTRACT_DIR"
    unzip -q "$BEEWARE_ARCHIVE" -d "$BEEWARE_EXTRACT_DIR"
  fi

  clear_staged_runtime_libs

  cp -f "$BEEWARE_EXTRACT_DIR"/libs/x86_64/libpython3.9.so "$OVERLAY_LIB_DIR"/
  cp -f "$BEEWARE_EXTRACT_DIR"/libs/x86_64/libffi.so "$OVERLAY_LIB_DIR"/
  cp -f "$BEEWARE_EXTRACT_DIR"/libs/x86_64/libsqlite3.so "$OVERLAY_LIB_DIR"/
  cp -f "$BEEWARE_EXTRACT_DIR"/libs/x86_64/libbz2.so "$OVERLAY_LIB_DIR"/
  cp -f "$BEEWARE_EXTRACT_DIR"/libs/x86_64/liblzma.so "$OVERLAY_LIB_DIR"/
  cp -f "$BEEWARE_EXTRACT_DIR"/libs/x86_64/libssl1.1.so "$OVERLAY_LIB_DIR"/
  cp -f "$BEEWARE_EXTRACT_DIR"/libs/x86_64/libcrypto1.1.so "$OVERLAY_LIB_DIR"/
  cp -f "$BEEWARE_EXTRACT_DIR"/libs/x86_64/libssl.so "$OVERLAY_LIB_DIR"/
  cp -f "$BEEWARE_EXTRACT_DIR"/libs/x86_64/libcrypto.so "$OVERLAY_LIB_DIR"/

  cp -f "$BEEWARE_EXTRACT_DIR"/src/main/assets/stdlib/*.x86_64.zip "$OVERLAY_STDLIB_DIR"/

  echo "Staged BeeWare CPython 3.9 runtime libs to: $OVERLAY_LIB_DIR"
  echo "Staged CPython stdlib zip to: $OVERLAY_STDLIB_DIR"
}

stage_termux_313() {
  mkdir -p "$TERMUX_WORK_DIR"
  download_if_missing "$TERMUX_PYTHON_URL" "$TERMUX_PYTHON_DEB"
  download_if_missing "$TERMUX_ANDROID_SUPPORT_URL" "$TERMUX_ANDROID_SUPPORT_DEB"

  rm -rf "$TERMUX_WORK_DIR/extract-python" "$TERMUX_WORK_DIR/extract-android-support"
  mkdir -p "$TERMUX_WORK_DIR/extract-python" "$TERMUX_WORK_DIR/extract-android-support"

  (
    cd "$TERMUX_WORK_DIR/extract-python"
    ar x "$TERMUX_PYTHON_DEB"
    tar xf data.tar.xz
  )
  (
    cd "$TERMUX_WORK_DIR/extract-android-support"
    ar x "$TERMUX_ANDROID_SUPPORT_DEB"
    tar xf data.tar.xz
  )

  clear_staged_runtime_libs

  cp -f "$TERMUX_WORK_DIR"/extract-python/data/data/com.termux/files/usr/lib/libpython3.13.so "$OVERLAY_LIB_DIR"/
  cp -f "$TERMUX_WORK_DIR"/extract-python/data/data/com.termux/files/usr/lib/libpython3.so "$OVERLAY_LIB_DIR"/
  cp -f "$TERMUX_WORK_DIR"/extract-android-support/data/data/com.termux/files/usr/lib/libandroid-support.so "$OVERLAY_LIB_DIR"/

  patchelf --set-rpath '$ORIGIN' "$OVERLAY_LIB_DIR"/libpython3.13.so
  patchelf --set-rpath '$ORIGIN' "$OVERLAY_LIB_DIR"/libpython3.so

  if ! ls "$OVERLAY_STDLIB_DIR"/*.zip >/dev/null 2>&1; then
    echo "No stdlib zip found in $OVERLAY_STDLIB_DIR. Run '$0 beeware-3.9' once to stage stdlib zip."
  fi

  echo "Staged Termux CPython 3.13 runtime libs to: $OVERLAY_LIB_DIR"
  echo "Patched RUNPATH for libpython3.13.so and libpython3.so to \$ORIGIN"
}

mkdir -p "$CACHE_DIR" "$OVERLAY_LIB_DIR" "$OVERLAY_STDLIB_DIR"

case "$RUNTIME" in
  beeware-3.9)
    stage_beeware_39
    ;;
  termux-3.13)
    stage_termux_313
    ;;
  *)
    usage
    exit 1
    ;;
esac
