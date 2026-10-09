#!/usr/bin/env bash
# Run every device-free firmware test: the C logic tests and the wire-format tests.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/../build"
mkdir -p "$OUT"

build() { cc "$@" -Wall -Wextra -Werror -I "$HERE/../al80-keyboard-src" "$HERE/test_logic.c" -o "$OUT/test_logic" 2>/dev/null; }
# Some macOS installs ship a default SDK the linker cannot read; fall back to the others.
build || { for sdk in /Library/Developer/CommandLineTools/SDKs/MacOSX*.sdk; do build -isysroot "$sdk" && break; done; }
"$OUT/test_logic"
(cd "$HERE" && node --test)
