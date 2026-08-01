#!/usr/bin/env python3
"""Split an 8bitworkshop Pengo combined ROM into MAME `pengo` / `pengou` ROM names.

8bitworkshop layout (src/machine/pengo.ts):
  0x0000-0x7FFF  program (32KB)
  0x8000-0x8FFF  tiles bank 0
  0x9000-0x9FFF  tiles bank 1
  0xA000-0xAFFF  sprites bank 0
  0xB000-0xBFFF  sprites bank 1
  0xC000-0xC01F  color PROM     → pr1633.ic78
  0xC100-0xC4FF  color lookup   → pr1634.ic88
  0xC500-0xC5FF  wave ROM       → pr1635.ic51

MAME gfx1 is interleaved (pengo.cpp ROM_CONTINUE):
  epr-1640.ic92  = tiles0 || sprites0
  epr-1695.ic105 = tiles1 || sprites1

Program is split into eight 4KB ROMs (pengou / non-encrypted naming):
  ep5128-ish labels vary by set; we emit generic 8bw names plus pengou-style
  aliases. Default output uses the `pengou` set filenames from MAME.

Usage:
  python3 scripts/8bw_pengo_to_mame.py path/to/game.rom [-o roms/pengo]
  mame pengou -rompath roms
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WAVE2 = ROOT / "pengo" / "pr1636.ic70"

# Non-encrypted set filenames (ROM_START(pengou) style / common bootleg names).
# MAME set `pengou` uses epr-5128… style in some trees; `pengo` is encrypted.
# We emit the widely used ep*/epr* names from ROM_START(pengo) program slots
# with the unikey set as a practical target: use --set pengou|pengo.
PROG_NAMES = {
    "pengou": [
        "epr-5128.ic8",
        "epr-5129.ic7",
        "epr-5130.ic15",
        "epr-5131a.ic14",
        "epr-5132.ic21",
        "epr-5133.ic20",
        "epr-5134.ic32",
        "epr-5135a.ic31",
    ],
    "pengo": [
        "epr-1738.ic8",
        "epr-1739.ic7",
        "epr-1740.ic15",
        "epr-1741.ic14",
        "epr-1742.ic21",
        "epr-1743.ic20",
        "epr-1744.ic32",
        "epr-1745.ic31",
    ],
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom", type=Path, help="64KB (or ≥0xC600) combined ROM from 8bitworkshop")
    ap.add_argument("-o", "--outdir", type=Path, default=Path("roms/pengo"), help="output directory")
    ap.add_argument(
        "--set",
        choices=sorted(PROG_NAMES),
        default="pengou",
        help="MAME set filenames for program ROMs (default: pengou)",
    )
    ap.add_argument(
        "--wave2",
        type=Path,
        default=DEFAULT_WAVE2,
        help=f"optional pr1636.ic70 timing PROM (default: {DEFAULT_WAVE2})",
    )
    args = ap.parse_args()

    data = args.rom.read_bytes()
    if len(data) < 0xc600:
        print(f"error: ROM too small ({len(data)} bytes); need at least 0xC600", file=sys.stderr)
        return 1
    if len(data) < 0x10000:
        print(f"warning: ROM is {len(data)} bytes (expected 65536); continuing", file=sys.stderr)

    args.outdir.mkdir(parents=True, exist_ok=True)
    names = PROG_NAMES[args.set]

    for i, name in enumerate(names):
        start = i * 0x1000
        chunk = data[start : start + 0x1000]
        (args.outdir / name).write_bytes(chunk)
        print(f"  wrote {args.outdir / name} (4096)")

    tiles0 = data[0x8000:0x9000]
    tiles1 = data[0x9000:0xa000]
    spr0 = data[0xa000:0xb000]
    spr1 = data[0xb000:0xc000]

    # MAME ROM_CONTINUE layout
    ep1640 = tiles0 + spr0
    ep1695 = tiles1 + spr1
    (args.outdir / "epr-1640.ic92").write_bytes(ep1640)
    (args.outdir / "epr-1695.ic105").write_bytes(ep1695)
    print(f"  wrote {args.outdir / 'epr-1640.ic92'} (tiles0+spr0)")
    print(f"  wrote {args.outdir / 'epr-1695.ic105'} (tiles1+spr1)")

    (args.outdir / "pr1633.ic78").write_bytes(data[0xc000:0xc020])
    (args.outdir / "pr1634.ic88").write_bytes(data[0xc100:0xc500])
    (args.outdir / "pr1635.ic51").write_bytes(data[0xc500:0xc600])
    print("  wrote pr1633.ic78 / pr1634.ic88 / pr1635.ic51")

    if args.wave2.is_file():
        w2 = args.wave2.read_bytes()
        if len(w2) != 256:
            print(f"error: {args.wave2} must be 256 bytes", file=sys.stderr)
            return 1
        (args.outdir / "pr1636.ic70").write_bytes(w2)
        print(f"  wrote pr1636.ic70 (from {args.wave2})")
    else:
        # MAME marks timing PROM unused; duplicate wave ROM so the file exists
        (args.outdir / "pr1636.ic70").write_bytes(data[0xc500:0xc600])
        print("  wrote pr1636.ic70 (copied wave ROM; timing PROM unused in MAME)", file=sys.stderr)

    print(f"\nDone. Try:  mame {args.set} -rompath {args.outdir.parent}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
