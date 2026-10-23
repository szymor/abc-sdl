#!/usr/bin/env bash
# Build the small, decode-only FFmpeg subset used by the Android port.
#
# The 210 original AVI clips use only cinepak, msvideo1 and indeo3 video with
# PCM s16le/u8 audio, so the library can be much smaller than a full FFmpeg.
#
# Usage:
#   NDK=/path/to/ndk tools/ndk-build-ffmpeg.sh [abi ...]
# Default ABIs: arm64-v8a x86_64
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NDK="${NDK:-${ANDROID_NDK_HOME:-$HOME/Android/Sdk/ndk/28.2.13676358}}"
FFMPEG_VERSION="7.1.5"
SRC="${FFMPEG_SRC:-/tmp/abc-sdl-ffmpeg/ffmpeg-${FFMPEG_VERSION}}"
OUT="${ROOT}/third_party/ffmpeg"
API="${ANDROID_API:-24}"

if [ ! -d "$SRC" ]; then
  echo "downloading FFmpeg ${FFMPEG_VERSION} sources..."
  mkdir -p "$(dirname "$SRC")"
  curl -L --fail --retry 3 -o "$(dirname "$SRC")/ffmpeg.tar.xz" \
    "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz"
  tar -xf "$(dirname "$SRC")/ffmpeg.tar.xz" -C "$(dirname "$SRC")"
fi
if [ ! -d "$NDK" ]; then
  echo "NDK not found: $NDK" >&2
  exit 1
fi

TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64"
if [ ! -d "$TOOLCHAIN" ]; then
  TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/darwin-x86_64"
fi

build_abi() {
  local abi="$1" arch cpu cc cxx
  case "$abi" in
    arm64-v8a)
      arch=aarch64; cpu=armv8-a
      cc="$TOOLCHAIN/bin/aarch64-linux-android${API}-clang"
      cxx="$TOOLCHAIN/bin/aarch64-linux-android${API}-clang++"
      ;;
    x86_64)
      arch=x86_64; cpu=x86-64
      cc="$TOOLCHAIN/bin/x86_64-linux-android${API}-clang"
      cxx="$TOOLCHAIN/bin/x86_64-linux-android${API}-clang++"
      ;;
    *)
      echo "unsupported ABI: $abi" >&2
      return 1
      ;;
  esac

  local build="/tmp/opencode/ffmpeg-build-${abi}"
  local prefix="$OUT/$abi"
  local asm_flag=""
  # The x86 assembly in this FFmpeg release is not PIC-safe for Android's
  # shared-library link.  The clips are only 320x240, so the C fallback is
  # plenty fast and avoids a second build configuration.
  if [ "$abi" = "x86_64" ]; then
    asm_flag="--disable-asm"
  fi
  rm -rf "$build"
  cp -a "$SRC" "$build"
  cd "$build"

  ./configure \
    --prefix="$prefix" \
    --target-os=android \
    --arch="$arch" \
    --cpu="$cpu" \
    --enable-cross-compile \
    --cc="$cc" \
    --cxx="$cxx" \
    --ar="$TOOLCHAIN/bin/llvm-ar" \
    --ranlib="$TOOLCHAIN/bin/llvm-ranlib" \
    --nm="$TOOLCHAIN/bin/llvm-nm" \
    --strip="$TOOLCHAIN/bin/llvm-strip" \
    --sysroot="$TOOLCHAIN/sysroot" \
    --enable-static \
    --disable-shared \
    --enable-pic \
    --enable-small \
    --disable-everything \
    $asm_flag \
    --disable-programs \
    --disable-doc \
    --disable-avdevice \
    --disable-postproc \
    --disable-network \
    --disable-autodetect \
    --disable-iconv \
    --disable-zlib \
    --disable-avfilter \
    --enable-avcodec \
    --enable-avformat \
    --enable-avutil \
    --enable-swscale \
    --enable-swresample \
    --enable-protocol=file \
    --enable-demuxer=avi \
    --enable-decoder=cinepak,msvideo1,indeo3,pcm_s16le,pcm_u8 \
    --extra-cflags="-O2 -fPIC" \
    --extra-ldflags="-fPIC"

  make -j"$(nproc)" >/tmp/opencode/ffmpeg-${abi}.log 2>&1
  make install >>/tmp/opencode/ffmpeg-${abi}.log 2>&1
  echo "built $abi -> $prefix"
}

if [ "$#" -eq 0 ]; then
  set -- arm64-v8a x86_64
fi
for abi in "$@"; do
  build_abi "$abi"
done
