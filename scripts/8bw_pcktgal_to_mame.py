#!/usr/bin/env python3
"""
Split an 8bitworkshop Pocket Gal homebrew ROM into a MAME `pcktgal2` set.

8bw linker output layout (pcktgal.cfg):
  0x00000..0x07FFF  program ROM  ($8000-$FFFF)
  0x08000..0x0FFFF  char ROM     (1024 tiles × 32 bytes, 4bpp planar)
  0x10000..0x13FFF  sprite ROM   (256 sprites × 64 bytes, 2bpp planar)
  0x14000..0x143FF  palette PROMs (512 + 512)

Char ROM notes (pcktgal2 / init_original):
  - MAME plane order {0x10000,0,0x18000,0x8000} is MSB-first → homebrew p0→0x8000
  - Driver init swaps 16-byte halves in every 32-byte block; we pre-swap so decode matches

Usage:
  python3 scripts/8bw_pcktgal_to_mame.py /tmp/hello.bin -o roms/pcktgal2
  mame pcktgal2 -rompath roms -window -skip_gameinfo
  # or: scripts/build_pcktgal_local.sh hello --run

Requires a stock pcktgal2 set for the audio CPU ROM (or pass --stub-audio).
"""
from __future__ import annotations

import argparse
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

EXPECTED_SIZE = 0x8000 + 0x8000 + 0x4000 + 0x400  # 82944

MAINCPU = "eb04-2.j7"
AUDIOCPU = "eb03-2.f2"
CHARS = ["eb01-2.rom", "eb02-2.rom"]
SPRITES = "eb00.a1"
PROMS = ["eb05.k14", "eb06.k15"]


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
        raise SystemExit(f"stock pcktgal2 set not found: {stock}")
    return files


def find_stock(explicit: Path | None) -> Path | None:
    if explicit and explicit.exists():
        return explicit
    candidates = [
        ROOT / "roms" / "pcktgal2",
        ROOT / "roms" / "pcktgal2.zip",
        Path.home() / "Downloads" / "pcktgal2.zip",
    ]
    for c in candidates:
        if c.exists():
            return c
    return None


def apply_pcktgal_char_line_swap(chars: bytearray) -> None:
    """Inverse of MAME pcktgal `init_original` (swap is an involution).

    Official sets store char ROM with 16-byte halves swapped in every 32-byte
    block; MAME undoes that at load. Homebrew assets are authored already in
    decode order, so we pre-swap here for pcktgal2.
    """
    for i in range(0, len(chars), 32):
        chars[i : i + 16], chars[i + 16 : i + 32] = (
            bytes(chars[i + 16 : i + 32]),
            bytes(chars[i : i + 16]),
        )


def homebrew_chars_to_mame(chars: bytes) -> bytes:
    """Expand 32KB homebrew planar chars → 128KB MAME char region."""
    out = bytearray(0x20000)
    ntiles = len(chars) // 32
    for t in range(ntiles):
        src = t * 32
        # homebrew: p0,p1,p2,p3 at +0,+8,+16,+24 (p0 = pixel bit0)
        # MAME charlayout planes {0x10000, 0, 0x18000, 0x8000} are MSB-first,
        # so bit0 is the last entry (0x8000).
        for p, mame_base in enumerate((0x08000, 0x18000, 0x00000, 0x10000)):
            out[mame_base + t * 8 : mame_base + t * 8 + 8] = chars[src + p * 8 : src + p * 8 + 8]
    apply_pcktgal_char_line_swap(out)
    return bytes(out)


def homebrew_sprites_to_mame(sprites: bytes) -> bytes:
    """Expand 16KB homebrew sprites → 64KB MAME sprite region."""
    out = bytearray(0x10000)
    nsprites = len(sprites) // 64
    for code in range(nsprites):
        src = code * 64
        for plane, plane_base in enumerate((0x0000, 0x8000)):
            hb = src + plane * 32
            mb = plane_base + code * 32
            for y in range(16):
                left = sprites[hb + y * 2]
                right = sprites[hb + y * 2 + 1]
                # MAME: right half at byte y, left half at byte 16+y
                out[mb + y] = right
                out[mb + 16 + y] = left
    return bytes(out)


def silent_audio_rom() -> bytes:
    """64KB audio ROM that just spins (plain M6502, pcktgal2-style)."""
    rom = bytearray(0x10000)
    # RESET/NMI/IRQ → $8000: sei; jmp $8000
    rom[0x8000] = 0x78  # SEI
    rom[0x8001] = 0x4C  # JMP
    rom[0x8002] = 0x00
    rom[0x8003] = 0x80
    rom[0xFFFA] = 0x00
    rom[0xFFFB] = 0x80
    rom[0xFFFC] = 0x00
    rom[0xFFFD] = 0x80
    rom[0xFFFE] = 0x00
    rom[0xFFFF] = 0x80
    return bytes(rom)


def load_audio_rom(path: Path | None, stub: bool) -> bytes:
    """Prefer homebrew YM2203 audio firmware; fall back to silent stub."""
    if stub:
        return silent_audio_rom()
    candidates = []
    if path:
        candidates.append(path)
    candidates.append(ROOT / "presets" / "pcktgal" / "audio.bin")
    for c in candidates:
        if c.exists() and c.stat().st_size == 0x10000:
            return c.read_bytes()
    print("note: no audio.bin; using silent stub (run scripts/build_pcktgal_audio.sh)")
    return silent_audio_rom()


def convert(
    data: bytes,
    stock: dict[str, bytes] | None,
    stub_audio: bool,
    audio_rom: Path | None = None,
) -> dict[str, bytes]:
    if len(data) != EXPECTED_SIZE:
        raise SystemExit(f"expected {EXPECTED_SIZE}-byte 8bw pcktgal ROM, got {len(data)}")

    prg = data[0:0x8000]
    chars = data[0x8000:0x10000]
    sprites = data[0x10000:0x14000]
    proms = data[0x14000:0x14400]

    maincpu = bytearray(0x10000)
    maincpu[0x8000:] = prg

    mame_chars = homebrew_chars_to_mame(chars)
    mame_sprites = homebrew_sprites_to_mame(sprites)

    # Prefer our YM2203 firmware over stock Pocket Gal audio (wrong game).
    if stub_audio:
        audiocpu = silent_audio_rom()
    else:
        audiocpu = load_audio_rom(audio_rom, stub=False)

    return {
        MAINCPU: bytes(maincpu),
        AUDIOCPU: audiocpu,
        CHARS[0]: mame_chars[0:0x10000],
        CHARS[1]: mame_chars[0x10000:0x20000],
        SPRITES: mame_sprites,
        PROMS[0]: proms[0:0x200],
        PROMS[1]: proms[0x200:0x400],
    }


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom", type=Path, help="8bw Pocket Gal binary (82944 bytes)")
    ap.add_argument("-s", "--stock", type=Path, default=None, help="stock pcktgal2 folder or zip")
    ap.add_argument("-o", "--out", type=Path, default=ROOT / "roms" / "pcktgal2", help="output folder")
    ap.add_argument("--stub-audio", action="store_true", help="use silent audio CPU ROM")
    ap.add_argument("--audio-rom", type=Path, default=None, help="64KB audio CPU image (default: presets/pcktgal/audio.bin)")
    args = ap.parse_args()

    data = args.rom.read_bytes()
    stock_path = find_stock(args.stock)
    stock = load_stock(stock_path) if stock_path else None

    files = convert(data, stock, args.stub_audio, args.audio_rom)
    args.out.mkdir(parents=True, exist_ok=True)
    for name, blob in files.items():
        (args.out / name).write_bytes(blob)
        print(f"  wrote {name} ({len(blob)} bytes)")
    print(f"done → {args.out}")
    print("run: mame pcktgal2 -rompath", args.out.parent, "-window -skip_gameinfo")


if __name__ == "__main__":
    main()
