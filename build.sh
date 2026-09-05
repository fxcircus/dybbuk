#!/usr/bin/env bash
# Build -> install -> validate. Run this before claiming anything is done.
set -euo pipefail

CONFIG="${1:-RelWithDebInfo}"
PRODUCT="Dybbuk"   # PRODUCT_NAME from CMakeLists.txt
AU_TYPE="aumf"              # aumf when NEEDS_MIDI_INPUT is TRUE, else aufx
AU_CODE="Dybk"              # PLUGIN_CODE
AU_MFR="Fxci"               # PLUGIN_MANUFACTURER_CODE

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build build

echo "--- pluginval ---"
/Applications/pluginval.app/Contents/MacOS/pluginval \
  --strictness-level 10 \
  --validate-in-process \
  --skip-gui-tests \
  --validate "$HOME/Library/Audio/Plug-Ins/VST3/${PRODUCT}.vst3"

if command -v auval >/dev/null 2>&1; then
  echo "--- auval ---"
  auval -v "$AU_TYPE" "$AU_CODE" "$AU_MFR" 2>&1 | tail -3
fi
