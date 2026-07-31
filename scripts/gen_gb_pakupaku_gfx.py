#!/usr/bin/env python3
"""
Convert arcade Pac-Man tiles/sprites (pacman.5e / 5f) into GB 2bpp data
embedded in presets/gb/pakupaku.c.

Usage:
  python3 scripts/gen_gb_pakupaku_gfx.py
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PAC = ROOT / "pacman"
GB = ROOT / "presets" / "gb" / "pakupaku.c"

# Arcade sprite indices used by pakupaku
PAC_FRAMES = [0x2C, 0x2E, 0x30]
DEATH_FRAMES = list(range(0x34, 0x3F))  # 11 frames
GHOST_FRAMES = [0x20, 0x21]
SCARED_FRAMES = [0x1C, 0x1D]
TILE_IDS = [0xDA, 0xDC, 0x10, 0x14]  # wall above/below, dot, power


def decode_strip(data8: bytes) -> list[list[int]]:
    rows = [[0] * 8 for _ in range(4)]
    for x in range(8):
        strip = data8[x]
        for y in range(4):
            pen = (1 if (strip & (1 << y)) else 0) | (
                2 if (strip & (1 << (y + 4))) else 0
            )
            rows[3 - y][7 - x] = pen
    return rows


def decode_tile(rom16: bytes) -> list[list[int]]:
    bot = decode_strip(rom16[0:8])
    top = decode_strip(rom16[8:16])
    return top + bot


def decode_sprite(rom64: bytes) -> list[list[int]]:
    layouts = [
        (8, 12),
        (8, 0),
        (8, 4),
        (8, 8),
        (0, 12),
        (0, 0),
        (0, 4),
        (0, 8),
    ]
    pix = [[0] * 16 for _ in range(16)]
    for i, (bx, by) in enumerate(layouts):
        rows = decode_strip(rom64[i * 8 : (i + 1) * 8])
        for dy in range(4):
            for dx in range(8):
                pix[by + dy][bx + dx] = rows[dy][dx]
    return pix


def map_pen(c: int) -> int:
    # Keep arcade pens; GB OBP 0xE4 reads 1=light, 2=mid, 3=dark.
    return c & 3


def gb_tile_2bpp(tile8: list[list[int]]) -> list[int]:
    out: list[int] = []
    for y in range(8):
        lo = hi = 0
        for x in range(8):
            v = map_pen(tile8[y][x])
            bit = 7 - x
            if v & 1:
                lo |= 1 << bit
            if v & 2:
                hi |= 1 << bit
        out.extend([lo, hi])
    return out


def sprite_to_gb_tiles(pix16: list[list[int]]) -> list[int]:
    tiles: list[int] = []
    for ox in (0, 8):
        for oy in (0, 8):
            t = [[pix16[oy + y][ox + x] for x in range(8)] for y in range(8)]
            tiles.extend(gb_tile_2bpp(t))
    return tiles


def eyes_only(pix: list[list[int]]) -> list[list[int]]:
    return [[(1 if c == 1 else 0) for c in row] for row in pix]


def scare_contrast(pix: list[list[int]]) -> list[list[int]]:
    """Body pen2→dark(3); face detail pen3→light(1) so eyes/mouth read on mono."""
    out = []
    for row in pix:
        out.append([{0: 0, 1: 1, 2: 3, 3: 1}.get(c, c) for c in row])
    return out


def c_array(name: str, data: list[int]) -> str:
    lines = [
        f"const uint8_t {name}[] = {{",
        f"/*{{w:8,h:8,bpp:1,count:{len(data) // 16},brev:1,np:2,pofs:1,sl:2}}*/",
    ]
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        lines.append("  " + ",".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> None:
    til = (PAC / "pacman.5e").read_bytes()
    spr = (PAC / "pacman.5f").read_bytes()

    sprite_bytes: list[int] = []
    for idx in PAC_FRAMES + DEATH_FRAMES + GHOST_FRAMES:
        pix = decode_sprite(spr[idx * 64 : (idx + 1) * 64])
        sprite_bytes.extend(sprite_to_gb_tiles(pix))
    for idx in SCARED_FRAMES:
        pix = scare_contrast(decode_sprite(spr[idx * 64 : (idx + 1) * 64]))
        sprite_bytes.extend(sprite_to_gb_tiles(pix))
    sprite_bytes.extend(
        sprite_to_gb_tiles(eyes_only(decode_sprite(spr[0x20 * 64 : 0x21 * 64])))
    )

    bkg_bytes: list[int] = []
    for tid in TILE_IDS:
        bkg_bytes.extend(gb_tile_2bpp(decode_tile(til[tid * 16 : (tid + 1) * 16])))

    text = GB.read_text()
    text2 = re.sub(
        r"const uint8_t bkg_tiles\[\] = \{.*?\n\};",
        c_array("bkg_tiles", bkg_bytes),
        text,
        count=1,
        flags=re.S,
    )
    text2 = re.sub(
        r"const uint8_t sprite_tiles\[\] = \{.*?\n\};",
        c_array("sprite_tiles", sprite_bytes),
        text2,
        count=1,
        flags=re.S,
    )
    if text2 == text:
        raise SystemExit("failed to patch arrays in presets/gb/pakupaku.c")
    GB.write_text(text2)
    nframes = (
        len(PAC_FRAMES)
        + len(DEATH_FRAMES)
        + len(GHOST_FRAMES)
        + len(SCARED_FRAMES)
        + 1
    )
    print(
        f"updated {GB.relative_to(ROOT)}: "
        f"{len(sprite_bytes) // 16} sprite tiles ({nframes} frames), "
        f"{len(bkg_bytes) // 16} bg tiles"
    )


if __name__ == "__main__":
    main()
