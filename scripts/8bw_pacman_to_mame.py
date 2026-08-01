#!/usr/bin/env python3
"""Split an 8bitworkshop Pac-Man 32KB combined ROM into MAME `pacman` ROM names.

8bitworkshop layout (see src/machine/pacman.ts):
  0x0000-0x3FFF  program (16KB)
  0x4000-0x4FFF  tile ROM   → pacman.5e
  0x5000-0x5FFF  sprite ROM → pacman.5f
  0x6000-0x601F  color PROM → 82s123.7f
  0x6100-0x61FF  palette    → 82s126.4a
  0x6200-0x62FF  wave ROM   → 82s126.1m

Program is split into the four 4KB CPU ROMs MAME expects:
  pacman.6e / 6f / 6h / 6j

82s126.3m is not in the 32KB image (only 1m is embedded). By default the
script copies pacman/82s126.3m from this repo; pass --wave3m to override.

Usage:
  python3 scripts/8bw_pacman_to_mame.py path/to/game.rom [-o roms/pacman]
  mame pacman -rompath roms
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WAVE3 = ROOT / "pacman" / "82s126.3m"

# (filename, start, size) — sizes match `mame pacman -listroms`
SLICES = (
    ("pacman.6e", 0x0000, 0x1000),
    ("pacman.6f", 0x1000, 0x1000),
    ("pacman.6h", 0x2000, 0x1000),
    ("pacman.6j", 0x3000, 0x1000),
    ("pacman.5e", 0x4000, 0x1000),
    ("pacman.5f", 0x5000, 0x1000),
    ("82s123.7f", 0x6000, 0x0020),
    ("82s126.4a", 0x6100, 0x0100),
    ("82s126.1m", 0x6200, 0x0100),
)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom", type=Path, help="32KB (or larger) combined ROM from 8bitworkshop")
    ap.add_argument(
        "-o",
        "--outdir",
        type=Path,
        default=Path("roms/pacman"),
        help="output directory (default: roms/pacman)",
    )
    ap.add_argument(
        "--wave3m",
        type=Path,
        default=DEFAULT_WAVE3,
        help=f"source for 82s126.3m (default: {DEFAULT_WAVE3})",
    )
    args = ap.parse_args()

    data = args.rom.read_bytes()
    if len(data) < 0x6300:
        print(f"error: ROM too small ({len(data)} bytes); need at least 0x6300", file=sys.stderr)
        return 1
    if len(data) < 0x8000:
        print(f"warning: ROM is {len(data)} bytes (expected 32768); continuing", file=sys.stderr)

    args.outdir.mkdir(parents=True, exist_ok=True)

    for name, start, size in SLICES:
        chunk = data[start : start + size]
        if len(chunk) != size:
            print(f"error: short read for {name}", file=sys.stderr)
            return 1
        out = args.outdir / name
        out.write_bytes(chunk)
        print(f"  wrote {out} ({size} bytes)")

    wave3_src = args.wave3m
    if wave3_src.is_file():
        w3 = wave3_src.read_bytes()
        if len(w3) != 256:
            print(f"error: {wave3_src} must be 256 bytes (got {len(w3)})", file=sys.stderr)
            return 1
        out = args.outdir / "82s126.3m"
        out.write_bytes(w3)
        print(f"  wrote {out} (from {wave3_src})")
    else:
        # Fallback: duplicate 1m so MAME can at least find the file
        w1 = data[0x6200:0x6300]
        out = args.outdir / "82s126.3m"
        out.write_bytes(w1)
        print(f"  wrote {out} (copied 82s126.1m; {wave3_src} missing)", file=sys.stderr)

    print(f"\nDone. Run e.g.:  mame pacman -rompath {args.outdir.parent}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
