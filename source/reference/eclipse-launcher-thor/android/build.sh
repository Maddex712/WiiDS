#!/usr/bin/env bash
set -euo pipefail
launcher_root=$(cd "$(dirname "$0")/.." && pwd)
port_root=${SMS_PORT_ROOT:-$(dirname "$launcher_root")/sms-port}
sdk_root=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
[[ -n "$sdk_root" ]] || { echo 'Set ANDROID_HOME to your Android SDK.' >&2; exit 1; }
export ANDROID_HOME="$sdk_root" ANDROID_SDK_ROOT="$sdk_root"
[[ -f "$port_root/android/build-native.sh" ]] || { echo 'Set SMS_PORT_ROOT to the Android port checkout.' >&2; exit 1; }
"$port_root/android/build-native.sh" --prepare
cd "$launcher_root/android"
./gradlew :app:assembleDebug "-PsmsPortRoot=$port_root" "${@}"
