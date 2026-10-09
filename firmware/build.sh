#!/usr/bin/env bash
# Build the AL80 firmware on macOS or Linux.
#
# Usage:  ./build.sh [keymap]        keymap: universal (default) or vial
#
# Needs: a vial-qmk checkout (QMK_HOME, default ../../vial-qmk) at the pinned commit with
# its ChibiOS submodules, arm-none-eabi-gcc on PATH and the qmk CLI.
set -euo pipefail

KEYMAP="${1:-universal}"
HERE="$(cd "$(dirname "$0")" && pwd)"
QMK_HOME="${QMK_HOME:-$HERE/../../vial-qmk}"
QMK_COMMIT="dd43959ae5c08d8a28d38a1acf7b04e86b14a344"
PATCH="$HERE/al80-keyboard-src/patches/vial-qmk-core.patch"
EEPROM_BASE=$((0x0801E000)) # wear-levelling store: code must end below it
FLASH_BASE=$((0x08002000))  # after the 8 KB bootloader

cd "$QMK_HOME"
[ "$(git rev-parse HEAD)" = "$QMK_COMMIT" ] || echo "warning: vial-qmk is not at $QMK_COMMIT"

# Core changes: 64-byte raw HID, LED flush guard during LCD transfers, Apple Fn usage.
if git apply --reverse --check "$PATCH" 2>/dev/null; then
  echo "core patch already applied"
else
  git apply "$PATCH"
fi

rm -rf keyboards/yunzii/al80
mkdir -p keyboards/yunzii/al80
cp -R "$HERE/al80-keyboard-src/." keyboards/yunzii/al80/
rm -rf keyboards/yunzii/al80/patches

make "yunzii/al80:$KEYMAP" -j8 >/dev/null
BIN=".build/yunzii_al80_$KEYMAP.bin"
USED=$(wc -c <"$BIN" | tr -d ' ')
FREE=$((EEPROM_BASE - FLASH_BASE - USED))
[ "$FREE" -ge 0 ] || { echo "OVERFLOW by $((-FREE)) bytes"; exit 1; }

mkdir -p "$HERE/build"
cp "$BIN" "$HERE/build/al80_$KEYMAP.bin"
echo "built $HERE/build/al80_$KEYMAP.bin: $USED bytes, $FREE free"
