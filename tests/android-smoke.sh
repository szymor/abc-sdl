#!/usr/bin/env bash
# Best-effort on-device smoke test.  Requires a connected device with the
# debug APK installed and content already present in
# /sdcard/Android/data/pl.szymor.abc/files/abc.
#
# Usage: tests/android-smoke.sh [start_scene]
set -euo pipefail
START="${1:-1001}"
PKG=pl.szymor.abc
ADB=${ADB:-adb}

echo "== launch $START"
$ADB shell am force-stop "$PKG"
$ADB logcat -c
$ADB shell am start -n "$PKG/.SetupActivity" --es start "$START" >/dev/null
sleep 5

echo "== logs"
$ADB logcat -d -v brief | grep -E '^I/abc|^W/abc|^E/abc|FATAL|AndroidRuntime' | tail -40

if $ADB logcat -d -v brief | grep -q 'FATAL EXCEPTION'; then
    echo "native/java crash detected" >&2
    exit 1
fi
if ! $ADB logcat -d -v brief | grep -q "scene $START loaded"; then
    echo "scene $START was not loaded (is content imported?)" >&2
    exit 1
fi
echo "ok"
