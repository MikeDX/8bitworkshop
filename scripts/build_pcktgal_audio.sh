#!/usr/bin/env bash
# Assemble Pocket Gal audio CPU ROM (YM2203 SSG sequencer).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$ROOT/presets/pcktgal/audio.bin}"
BUILD=/tmp/pcktgal-audio-build
mkdir -p "$BUILD"

ca65 -I "$ROOT/presets/pcktgal" "$ROOT/presets/pcktgal/audio.s" -o "$BUILD/audio.o"
ld65 -C "$ROOT/presets/pcktgal/audio.cfg" -o "$BUILD/audio.raw" "$BUILD/audio.o"
python3 - "$BUILD/audio.raw" "$OUT" <<'PY'
import sys
from pathlib import Path
raw = Path(sys.argv[1]).read_bytes()
out = Path(sys.argv[2])
rom = bytearray(b"\xff" * 0x10000)
if len(raw) > 0x8000:
    raise SystemExit(f"code too big: {len(raw)}")
rom[0x8000 : 0x8000 + len(raw)] = raw
out.write_bytes(rom)
print(f"wrote {out}: code={len(raw)}")
PY
