#!/usr/bin/env bash

if [ -z "$ANDROID_HOME" ]; then
  if test -d "$HOME/android"; then
    ANDROID_HOME="$HOME/android"
  elif test -d "$HOME/Android"; then
    ANDROID_HOME="$HOME/Android"
  elif test -d "$HOME/Library/Android/sdk"; then
    ANDROID_HOME="$HOME/Library/Android/sdk"
  elif test -d "$HOME/Library/Android"; then
    ANDROID_HOME="$HOME/Library/Android"
  elif test -d "$HOME/.android/sdk"; then
    ANDROID_HOME="$HOME/.android/sdk"
  elif test -d "$HOME/.android"; then
    ANDROID_HOME="$HOME/.android"
  fi
fi

emulator_flags=()
emulator="$(which emulator 2>/dev/null)"
avdmanager="$(which avdmanager 2>/dev/null)"
sdkmanager="$(which sdkmanager 2>/dev/null)"

if [ -z "$emulator" ]; then
  emulator="$ANDROID_HOME/emulator/emulator"
fi

if [ -z "$avdmanager" ]; then
  avdmanager="$ANDROID_HOME/cmdline-tools/tools/bin/avdmanager"
fi

if [ -z "$sdkmanager" ]; then
  sdkmanager="$ANDROID_HOME/cmdline-tools/tools/bin/sdkmanager"
fi

emulator_flags+=(
  -camera-back none
  -no-boot-anim
  -no-window
  -noaudio
)

if ! "$avdmanager" list avd | grep 'Name: SSCAVD$'; then
  pkg='system-images;android-33;google_apis;x86_64'
  "$sdkmanager" "$pkg" || exit $?
  echo yes | "$avdmanager" --clear-cache create avd -n SSCAVD -k "$pkg" -d 'pixel_5' --force || exit $?
fi

"$emulator" @SSCAVD "${emulator_flags[@]}" >/dev/null
