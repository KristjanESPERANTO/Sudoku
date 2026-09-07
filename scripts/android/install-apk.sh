#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APK_PATH="${1:-$REPO_ROOT/android/app/build/outputs/apk/debug/app-debug.apk}"
DEVICE="${ANDROID_SERIAL:-}"
ADB_BIN="${ADB_BIN:-}"

if [[ -z "$ADB_BIN" ]]; then
  if command -v adb >/dev/null 2>&1; then
    ADB_BIN="$(command -v adb)"
  elif [[ -n "${ANDROID_SDK_ROOT:-}" && -x "${ANDROID_SDK_ROOT}/platform-tools/adb" ]]; then
    ADB_BIN="${ANDROID_SDK_ROOT}/platform-tools/adb"
  elif [[ -n "${ANDROID_HOME:-}" && -x "${ANDROID_HOME}/platform-tools/adb" ]]; then
    ADB_BIN="${ANDROID_HOME}/platform-tools/adb"
  elif [[ -x "$HOME/Android/Sdk/platform-tools/adb" ]]; then
    ADB_BIN="$HOME/Android/Sdk/platform-tools/adb"
  fi
fi

if [[ ! -f "$APK_PATH" ]]; then
  echo "APK not found: $APK_PATH"
  echo "Run scripts/android/build-apk.sh first."
  exit 1
fi

if [[ -z "$ADB_BIN" || ! -x "$ADB_BIN" ]]; then
  echo "adb not found. Set ADB_BIN or add adb to PATH."
  exit 1
fi

if [[ -n "$DEVICE" ]]; then
  "$ADB_BIN" -s "$DEVICE" install -r "$APK_PATH"
else
  "$ADB_BIN" install -r "$APK_PATH"
fi
