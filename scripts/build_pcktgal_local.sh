#!/usr/bin/env bash
# Compile a Pocket Gal preset, convert to MAME pcktgal2, optionally run.
#
# Usage:
#   scripts/build_pcktgal_local.sh [preset] [--run] [--out DIR]
#   scripts/build_pcktgal_local.sh hello --run
#   scripts/build_pcktgal_local.sh pacman --run
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PRESET="${1:-hello}"
OUT_DIR="$ROOT/roms/pcktgal2"
DO_RUN=0
shift || true
while [[ $# -gt 0 ]]; do
  case "$1" in
    --run) DO_RUN=1 ;;
    --out) OUT_DIR="$2"; shift ;;
    *) echo "unknown arg: $1" >&2; exit 1 ;;
  esac
  shift
done

if [[ "$PRESET" == *.c ]]; then
  SRC="$PRESET"
else
  SRC="presets/pcktgal/${PRESET}.c"
fi
if [[ ! -f "$SRC" ]]; then
  echo "missing source: $SRC" >&2
  exit 1
fi

NAME="$(basename "$SRC" .c)"
BIN="/tmp/pcktgal-${NAME}.bin"
CFG="src/worker/lib/pcktgal/pcktgal.cfg"
CRT0="src/worker/lib/pcktgal/crt0.o"
LIB="$(dirname "$(which cl65)")/../share/cc65/lib/none.lib"
# Homebrew installs may use Cellar path
if [[ ! -f "$LIB" ]]; then
  LIB="$(echo /opt/homebrew/Cellar/cc65/*/share/cc65/lib/none.lib | awk '{print $1}')"
fi

echo "compile $SRC → $BIN"

if [[ "$NAME" == "pacman" ]]; then
  # Huge gfx inits crash cc65 -O; actors also safer without -O
  python3 scripts/gen_pcktgal_pacman_gfx.py
  scripts/build_pcktgal_audio.sh
  BUILD=/tmp/pcktgal-pac-build
  mkdir -p "$BUILD"
  INC="$ROOT/presets/pcktgal"
  compile_one() {
    local f=$1 opt=$2
    local base
    base=$(basename "$f" .c)
    if [[ "$opt" == "1" ]]; then
      cc65 -t none -O -I "$INC" "$f" -o "$BUILD/$base.s"
    else
      cc65 -t none -I "$INC" "$f" -o "$BUILD/$base.s"
    fi
    ca65 -t none "$BUILD/$base.s" -o "$BUILD/$base.o"
  }
  compile_one "$INC/pacman_gfx.c" 0
  compile_one "$INC/pacman_actors.c" 0
  compile_one "$INC/pacman.c" 1
  compile_one "$INC/pacman_common.c" 1
  compile_one "$INC/pacman_sfx.c" 1
  compile_one "$INC/pacman_maze.c" 1
  compile_one "$INC/pacman_render.c" 1
  ld65 -C "$CFG" -o "$BIN" \
    "$CRT0" \
    "$BUILD"/pacman.o "$BUILD"/pacman_common.o "$BUILD"/pacman_gfx.o \
    "$BUILD"/pacman_sfx.o "$BUILD"/pacman_maze.o "$BUILD"/pacman_actors.o \
    "$BUILD"/pacman_render.o \
    "$LIB"
else
  cl65 -t none -O -C "$CFG" "$CRT0" "$SRC" -o "$BIN"
fi

echo "convert → $OUT_DIR (pcktgal2)"
python3 scripts/8bw_pcktgal_to_mame.py "$BIN" -o "$OUT_DIR"

if [[ "$DO_RUN" -eq 1 ]]; then
  ROMPATH="$(dirname "$OUT_DIR")"
  # Pac-Man framebuffer is 90° CW; -rol presents it upright (portrait).
  EXTRA=()
  if [[ "$NAME" == "pacman" ]]; then
    EXTRA+=(-rol)
  fi
  echo "run: mame pcktgal2 -rompath $ROMPATH -window -skip_gameinfo ${EXTRA[*]-}"
  exec mame pcktgal2 -rompath "$ROMPATH" -window -skip_gameinfo "${EXTRA[@]}"
fi

echo
echo "Run with:"
if [[ "$NAME" == "pacman" ]]; then
  echo "  mame pcktgal2 -rompath $(dirname "$OUT_DIR") -window -skip_gameinfo -rol"
else
  echo "  mame pcktgal2 -rompath $(dirname "$OUT_DIR") -window -skip_gameinfo"
fi
