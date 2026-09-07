#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ANDROID_DIR="$REPO_ROOT/android"

"$REPO_ROOT/scripts/android/setup-runtime.sh"

cd "$ANDROID_DIR"
./gradlew :app:assembleDebug "$@"

echo
echo "Built APK: $ANDROID_DIR/app/build/outputs/apk/debug/app-debug.apk"
