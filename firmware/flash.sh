#!/usr/bin/env bash
# Flash a built firmware to the AL80 over the stm32duino bootloader.
#
# Usage:  ./flash.sh [keymap]        keymap: universal (default) or vial
#
# If the keyboard is not already in the bootloader, a development build is asked to
# enter it over raw HID. Otherwise hold Fn + Right Shift for 3 seconds, or unplug,
# hold Esc and plug back in.
set -euo pipefail

KEYMAP="${1:-universal}"
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="$HERE/build/al80_$KEYMAP.bin"
PY="${AL80_PY:-python3}" # needs the hidapi package
[ -f "$BIN" ] || { echo "no $BIN, run ./build.sh $KEYMAP first"; exit 1; }

in_dfu() { dfu-util -l 2>/dev/null | grep -q "1eaf:0003"; }

if ! in_dfu; then
  "$PY" - <<'PY' || true
import hid
for vid, pid in ((0x05AC, 0x029C), (0x28E9, 0x30AF)):
    for d in hid.enumerate(vid, pid):
        if d["usage_page"] == 0xFF60 and d["usage"] == 0x61:
            h = hid.device(); h.open_path(d["path"])
            h.write([0x00, 0x4E] + list(b"BOOT") + [0] * 59)
            h.close()
            print("asked the keyboard to enter the bootloader")
PY
  for _ in $(seq 1 20); do in_dfu && break; sleep 0.5; done
fi
in_dfu || { echo "keyboard is not in the bootloader"; exit 1; }

# dfu-util can hang after the keyboard restarts, so it runs under a watchdog.
dfu-util -d 1eaf:0003 -a 2 -D "$BIN" -R >"$HERE/build/flash.log" 2>&1 &
PID=$!
for _ in $(seq 1 90); do kill -0 "$PID" 2>/dev/null || break; sleep 1; in_dfu || break; done
sleep 2
kill "$PID" 2>/dev/null || true
grep -q "Download done\|File downloaded successfully" "$HERE/build/flash.log" && echo "flashed $BIN" || { tr '\r' '\n' <"$HERE/build/flash.log" | tail -5; exit 1; }
