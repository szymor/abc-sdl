#!/usr/bin/env bash
# Download the pinned SDL3 Android archives used by android/app/build.gradle.
# The archives are not committed because they are large binary dependencies.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/android/app/libs"
TMP="${TMPDIR:-/tmp}/abc-sdl-android-deps.$$"
mkdir -p "$DEST" "$TMP"
trap 'rm -rf "$TMP"' EXIT

fetch_aar() {
  local name="$1" url="$2"
  if [ -f "$DEST/$name" ]; then
    echo "present: $name"
    return
  fi
  echo "downloading: $name"
  curl -L --fail --retry 3 -o "$TMP/pkg.zip" "$url"
  unzip -q -o "$TMP/pkg.zip" -d "$TMP/pkg"
  cp "$TMP/pkg/$name" "$DEST/$name"
  rm -rf "$TMP/pkg" "$TMP/pkg.zip"
}

fetch_aar "SDL3-3.4.18.aar" \
  "https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-android.zip"
fetch_aar "SDL3_image-3.4.8.aar" \
  "https://github.com/libsdl-org/SDL_image/releases/download/release-3.4.8/SDL3_image-devel-3.4.8-android.zip"
fetch_aar "SDL3_mixer-3.2.4.aar" \
  "https://github.com/libsdl-org/SDL_mixer/releases/download/release-3.2.4/SDL3_mixer-devel-3.2.4-android.zip"

echo "done: $DEST"
