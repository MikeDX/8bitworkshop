#!/usr/bin/env python3
"""
Split an 8bitworkshop MCR (91490) homebrew ROM into a MAME `timber` set.

8bw download blob layout (see src/machine/mcr.ts):
  0x0000..0x7FFF  program ROM
  0x8000..0x9FFF  BG gfx (mcr_bg_layout window: low half + high half)
  0xA000..0xBFFF  sprite gfx (mcr_sprite_layout: 4 quarters × 2KB)

Usage:
  python3 scripts/8bw_mcr_to_mame.py path/to/chase.bin -o roms/timber \\
      --stock ~/Downloads/timber.zip
  mame timber -rompath roms -window -skip_gameinfo

Requires a stock timber.zip (or timber/ folder) for SSIO sound ROMs + PROM.
"""
from __future__ import annotations

import argparse
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# MAME timber maincpu loads
CPU_ROMS = [
    ("timpg0.bin", 0x0000, 0x4000),
    ("timpg1.bin", 0x4000, 0x4000),
    ("timpg2.bin", 0x8000, 0x4000),
    ("timpg3.bin", 0xC000, 0x2000),
]

SSIO_ROMS = ["tima7.bin", "tima8.bin", "tima9.bin"]
PROM_ROMS = ["82s123.12d"]

# gfx1: timbg1 @0, timbg0 @0x4000 (each 0x4000)
# gfx2: timfg1,0,3,2,5,4,7,6 — each 0x4000 covering 8 quarters of the 128KB region
FG_NAMES = [
    "timfg1.bin",
    "timfg0.bin",
    "timfg3.bin",
    "timfg2.bin",
    "timfg5.bin",
    "timfg4.bin",
    "timfg7.bin",
    "timfg6.bin",
]

BLOB_SIZE = 0xC000
PAD = 0xFF


def load_stock(stock: Path) -> dict[str, bytes]:
    files: dict[str, bytes] = {}
    if stock.is_dir():
        for p in stock.iterdir():
            if p.is_file():
                files[p.name] = p.read_bytes()
    elif stock.suffix.lower() == ".zip":
        with zipfile.ZipFile(stock) as zf:
            for name in zf.namelist():
                base = Path(name).name
                if base:
                    files[base] = zf.read(name)
    else:
        raise SystemExit(f"stock timber set not found: {stock}")
    missing = [n for n in SSIO_ROMS + PROM_ROMS if n not in files]
    if missing:
        raise SystemExit(f"stock set missing: {missing}")
    return files


def pad_to(data: bytes, size: int, fill: int = PAD) -> bytes:
    if len(data) >= size:
        return data[:size]
    return data + bytes([fill]) * (size - len(data))


def expand_bg(bg_2k: bytes) -> tuple[bytes, bytes]:
    """8bw 0x2000 window (lo 0x1000 + hi 0x1000) → timber 2×0x4000.

    MAME loads timbg1 @0 and timbg0 @0x4000. digfx is MSB-first, so
    timbg1 (low half) supplies color bits 1..0 for 2-bit homebrew tiles.
    """
    if len(bg_2k) < 0x2000:
        bg_2k = pad_to(bg_2k, 0x2000, 0)
    lo = pad_to(bg_2k[0:0x1000], 0x4000, 0)
    hi = pad_to(bg_2k[0x1000:0x2000], 0x4000, 0)
    return lo, hi  # timbg1, timbg0


def expand_sprites(spr_2k: bytes) -> list[bytes]:
    """8bw 4×0x800 quarters → 8×0x4000 timber FG chips.

    MAME gfx2 is 0x20000 with 4 fractions of 0x8000 each.
    Our blob holds 0x800 per quarter (16 sprites). Pad each quarter to 0x8000,
    then split each 0x8000 into two 0x4000 files (odd/even naming order).
    """
    if len(spr_2k) < 0x2000:
        spr_2k = pad_to(spr_2k, 0x2000, 0)
    quarters = [pad_to(spr_2k[i * 0x800 : (i + 1) * 0x800], 0x8000, 0) for i in range(4)]
    # Load order: fg1, fg0 = quarter0 split? Looking at timber:
    #   timfg1 @0x00000, timfg0 @0x04000  → together fraction 0 (0x8000)
    #   timfg3 @0x08000, timfg2 @0x0C000  → fraction 1
    #   etc.
    out: list[bytes] = []
    for q in quarters:
        out.append(q[0:0x4000])       # *1.bin (lower)
        out.append(q[0x4000:0x8000])  # *0.bin (upper)
    # FG_NAMES order is 1,0,3,2,5,4,7,6 which matches appending (lo,hi) per quarter
    return out


def convert(blob: bytes, out_dir: Path, stock: dict[str, bytes]) -> None:
    if len(blob) < 0x8000:
        raise SystemExit(f"ROM too small ({len(blob)}); expected >= 32KB program")
    blob = pad_to(blob, BLOB_SIZE, PAD)

    prg = bytearray(blob[0:0x8000])
    # Program must not execute gfx as code — zero the CPU-visible gfx window in
    # the program image that goes into timpg* (gfx live only in gfx regions).
    # timpg2 covers 0x8000-0xBFFF on real hardware as MORE program ROM, so pad FF.
    bg = bytes(blob[0x8000:0xA000])
    spr = bytes(blob[0xA000:0xC000])

    out_dir.mkdir(parents=True, exist_ok=True)

    # Build a 56KB program image: 32KB homebrew + 24KB pad
    full_prg = pad_to(bytes(prg), 0xE000, PAD)
    for name, start, size in CPU_ROMS:
        (out_dir / name).write_bytes(full_prg[start : start + size])

    timbg1, timbg0 = expand_bg(bg)
    (out_dir / "timbg1.bin").write_bytes(timbg1)
    (out_dir / "timbg0.bin").write_bytes(timbg0)

    fg_parts = expand_sprites(spr)
    for name, data in zip(FG_NAMES, fg_parts):
        (out_dir / name).write_bytes(data)

    for name in SSIO_ROMS + PROM_ROMS:
        (out_dir / name).write_bytes(stock[name])

    print(f"wrote timber set → {out_dir}")
    print(f"  program: {len(prg)} bytes (padded into timpg0..3)")
    print(f"  bg:      {len(bg)} → timbg1/timbg0")
    print(f"  sprites: {len(spr)} → timfg*.bin")
    print(f"  ssio+prom: from stock")
    print()
    print(f"  mame timber -rompath {out_dir.parent} -window -skip_gameinfo")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom", type=Path, help="8bw MCR download .bin (48KB blob)")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "roms" / "timber",
                    help="output directory (default: roms/timber)")
    ap.add_argument(
        "--stock",
        type=Path,
        default=None,
        help="stock timber.zip or timber/ folder for SSIO+PROM",
    )
    args = ap.parse_args()

    stock_path = args.stock
    if stock_path is None:
        candidates = [
            ROOT / "roms" / "timber.zip",
            ROOT / "timber.zip",
            Path.home() / "Downloads" / "MAME 0.139 Rom Collection By RetroGOD" / "timber.zip",
            Path.home() / "Downloads" / "timber.zip",
            Path.home() / "mame" / "roms" / "timber.zip",
        ]
        for c in candidates:
            if c.exists():
                stock_path = c
                break
        if stock_path is None:
            raise SystemExit(
                "stock timber.zip not found; pass --stock /path/to/timber.zip"
            )

    data = args.rom.read_bytes()
    stock = load_stock(stock_path)
    print(f"stock: {stock_path}")
    convert(data, args.out, stock)


if __name__ == "__main__":
    main()
