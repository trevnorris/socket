#!/usr/bin/env bash

declare root="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"
declare arch=""
declare platform=""
declare do_full_clean=0

if (( TARGET_OS_IPHONE )); then
  arch="arm64"
  platform="iPhoneOS"
elif (( TARGET_IPHONE_SIMULATOR )); then
  arch="x86_64"
  platform="iPhoneSimulator"
fi

while (( $# > 0 )); do
  declare arg="$1"; shift
  if [[ "$arg" = "--arch" ]]; then
    arch="$1"; shift
    continue
  fi

  if [[ "$arg" = "--full" ]]; then
    do_full_clean=1
    continue
  fi

  if [[ "$arg" = "--platform" ]]; then
    if [[ "$1" = "ios" ]] || [[ "$1" = "iPhoneOS" ]] || [[ "$1" = "iphoneos" ]]; then
      arch="arm64"
      platform="iPhoneOS";
    elif [[ "$1" = "ios-simulator" ]] || [[ "$1" = "iPhoneSimulator" ]] || [[ "$1" = "iphonesimulator" ]]; then
      arch="x86_64"
      platform="iPhoneSimulator";
    else
      platform="$1";
    fi
    shift
    continue
  fi
done

declare targets=()

if [ -n "$arch" ] || [ -n "$platform" ]; then
  if (( do_full_clean )); then
    echo "error: cannot mix '--full' and '--arch/--platform'" >&2
    exit 1
  fi

  arch="${arch:-$(uname -m)}"
  platform="${platform:-desktop}"
  targets+=($(find                               \
    "$root/build/$arch-$platform"/app            \
    "$root/build/$arch-$platform"/bin            \
    "$root/build/$arch-$platform"/cli            \
    "$root/build/$arch-$platform"/runtime        \
    "$root/build/$arch-$platform"/ipc            \
    "$root/build/$arch-$platform"/core           \
    "$root/build/$arch-$platform"/objects        \
    "$root/build/$arch-$platform"/process        \
    "$root/build/$arch-$platform"/window         \
    "$root/build/$arch-$platform"/lib/libsocket* \
    "$root/build/$arch-$platform"/tests          \
    "$root/build/$arch-$platform"/*.o            \
    "$root/build/$arch-$platform"/**/*.o         \
    "$root/build/npm/$platform"                  \
  2>/dev/null))
elif (( do_full_clean )); then
  if test -f "$root/build"; then
    rm -rf "$root/build" || exit $?
    echo "ok - cleaned (full)"
  fi
else
  targets+=($(find                 \
    "$root"/build/*/app            \
    "$root"/build/*/bin            \
    "$root"/build/*/cli            \
    "$root"/build/*/runtime        \
    "$root"/build/*/core           \
    "$root"/build/*/ipc            \
    "$root"/build/*/objects        \
    "$root"/build/*/process        \
    "$root"/build/*/window         \
    "$root"/build/*/lib/libsocket* \
    "$root"/build/*/tests          \
    "$root"/build/*/*.o            \
    "$root"/build/*/**/*.o         \
    "$root"/build/npm              \
  2>/dev/null))
fi

if (( ${#targets[@]} > 0 )); then
  echo "# cleaning targets"
  for target in "${targets[@]}"; do
    if test "$target"; then
      rm -rf "$target"
      if [ -n "$VERBOSE" ]; then
        echo "ok - cleaned ${target/$root\//}"
      fi
    fi
  done
  echo "ok - cleaned"
fi
