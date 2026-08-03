#!/usr/bin/env python3
"""Convert Pac-Man arcade gfx (from pacmanonpacman assets) → pcktgal CHARS/SPRITES/PROMS.

  python3 scripts/gen_pcktgal_pacman_gfx.py

Reads presets/pacman/pacman_assets.c if present, else `git show pacmanonpacman:...`.
Writes presets/pcktgal/pacman_gfx.c
"""
from __future__ import annotations

import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "presets" / "pcktgal" / "pacman_gfx.c"


def load_assets_text() -> str:
    local = ROOT / "presets" / "pacman" / "pacman_assets.c"
    if local.exists():
        return local.read_text()
    return subprocess.check_output(
        ["git", "show", "pacmanonpacman:presets/pacman/pacman_assets.c"], text=True
    )


def grab(text: str, name: str, n: int) -> bytes:
    m = re.search(rf"{name}\s*\[[^\]]*\]\s*=\s*(?:/\*[^*]*\*/\s*)?\{{", text)
    if not m:
        raise SystemExit(f"missing {name}")
    i = m.end()
    vals: list[int] = []
    while len(vals) < n and i < len(text):
        mm = re.match(r"\s*(0x[0-9a-fA-F]+|\d+)\s*,?", text[i:])
        if mm:
            vals.append(int(mm.group(1), 0))
            i += mm.end()
            continue
        if text[i] in " \t\n\r,":
            i += 1
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = j + 2 if j >= 0 else i + 1
            continue
        if text[i] == "}":
            break
        i += 1
    if len(vals) != n:
        raise SystemExit(f"{name}: got {len(vals)}, want {n}")
    return bytes(vals)


def decode_strip(inp: bytes, in_off: int, out: list[int], bx: int, by: int, img_w: int) -> None:
    """Exact port of pacman.ts decodeStrip (mirrored pac-c layout)."""
    base = by * img_w + bx
    for x in range(8):
        strip = inp[in_off + x]
        for y in range(4):
            i = (3 - y) * img_w + (7 - x)
            pen = ((strip >> y) & 1) | (((strip >> (y + 4)) & 1) << 1)
            out[base + i] = pen


def decode_tile(src16: bytes) -> list[int]:
    out = [0] * 64
    decode_strip(src16, 0, out, 0, 4, 8)
    decode_strip(src16, 8, out, 0, 0, 8)
    return out


def decode_sprite(src64: bytes) -> list[int]:
    out = [0] * 256
    decode_strip(src64, 0 * 8, out, 8, 12, 16)
    decode_strip(src64, 1 * 8, out, 8, 0, 16)
    decode_strip(src64, 2 * 8, out, 8, 4, 16)
    decode_strip(src64, 3 * 8, out, 8, 8, 16)
    decode_strip(src64, 4 * 8, out, 0, 12, 16)
    decode_strip(src64, 5 * 8, out, 0, 0, 16)
    decode_strip(src64, 6 * 8, out, 0, 4, 16)
    decode_strip(src64, 7 * 8, out, 0, 8, 16)
    return out


def rot90_cw(pix: list[int], w: int, h: int) -> list[int]:
    """Rotate pixel grid 90° clockwise: (x,y) → (h-1-y, x). Square stays square."""
    out = [0] * (w * h)
    for y in range(h):
        for x in range(w):
            nx = h - 1 - y
            ny = x
            out[ny * h + nx] = pix[y * w + x]
    return out


def encode_char_tile(pix64: list[int]) -> bytes:
    pix64 = rot90_cw(pix64, 8, 8)
    out = bytearray(32)
    for p in range(4):
        for y in range(8):
            b = 0
            for x in range(8):
                if pix64[y * 8 + x] & (1 << p):
                    b |= 0x80 >> x
            out[p * 8 + y] = b
    return bytes(out)


def encode_sprite(pix256: list[int]) -> bytes:
    """Homebrew 64 B: plane0 then plane1, row-major left-then-right."""
    pix256 = rot90_cw(pix256, 16, 16)
    out = bytearray(64)
    for plane in range(2):
        for y in range(16):
            left = right = 0
            for x in range(8):
                if pix256[y * 16 + x] & (1 << plane):
                    left |= 0x80 >> x
                if pix256[y * 16 + 8 + x] & (1 << plane):
                    right |= 0x80 >> x
            out[plane * 32 + y * 2] = left
            out[plane * 32 + y * 2 + 1] = right
    return bytes(out)


def pac_rgb(ci: int) -> tuple[int, int, int]:
    """MAME-ish Pac-Man color PROM byte → 4-bit RGB nibbles for pcktgal PROM."""
    # color_prom is RR R GG G BB B (approx) — use same expansion as pacman.ts
    r = (((ci >> 0) & 1) * 0x21 + ((ci >> 1) & 1) * 0x47 + ((ci >> 2) & 1) * 0x97) >> 4
    g = (((ci >> 3) & 1) * 0x21 + ((ci >> 4) & 1) * 0x47 + ((ci >> 5) & 1) * 0x97) >> 4
    b = (((ci >> 6) & 1) * 0x51 + ((ci >> 7) & 1) * 0xae) >> 4
    return min(r, 15), min(g, 15), min(b, 15)


def build_proms(color: bytes, palette: bytes) -> bytes:
    """512 RG + 512 B.

    poke_tile uses (pal & 0x0f) as BAC06 colour, so PAL_MAZE(0x10)→bank0,
    PAL_DOOR(0x18)→bank8. Sprite set_sprite_ex maps Pac pals → banks 0..7.
    """
    rg = bytearray(512)
    bb = bytearray(512)

    def set_pen(pen: int, ci: int) -> None:
        r, g, b = pac_rgb(color[ci & 31])
        rg[pen] = (r & 0xF) | ((g & 0xF) << 4)
        bb[pen] = b & 0xF

    def fill_from_pal(base: int, stride: int, pal: int) -> None:
        o = (pal & 0x3F) * 4
        for pix in range(4):
            set_pen(base + pix * (stride // 4 if False else 1), palette[o + pix])
        # stride unused — pens are contiguous for sprites; for tiles +pix

    sprite_pals = {
        0: 0x09,  # Pac yellow
        1: 0x01,  # Blinky
        2: 0x14,  # fruit
        3: 0x03,  # Pinky
        4: 0x12,  # scared blink
        5: 0x05,  # Inky
        6: 0x11,  # scared
        7: 0x07,  # Clyde
    }
    for bank, pal in sprite_pals.items():
        o = (pal & 0x3F) * 4
        for pix in range(4):
            set_pen(bank * 4 + pix, palette[o + pix])

    # Tile banks 0..15
    for bank in range(16):
        pal = bank
        if bank == 0:
            pal = 0x10  # PAL_MAZE
        elif bank == 8:
            pal = 0x18  # PAL_DOOR
        o = (pal & 0x3F) * 4
        for pix in range(4):
            set_pen(256 + bank * 16 + pix, palette[o + pix])

    return bytes(rg) + bytes(bb)


def c_array(name: str, data: bytes, comment: str, cols: int = 16) -> str:
    # Omit size when ≥32768 — cc65 warns "Integer constant is long" for that.
    size = "" if len(data) >= 32768 else str(len(data))
    lines = [f"const unsigned char {name}[{size}] = {{ /*{comment}*/"]
    for i in range(0, len(data), cols):
        chunk = data[i : i + cols]
        lines.append("  " + ",".join(f"0x{b:02X}" for b in chunk) + ",")
    lines.append("};")
    return "\n".join(lines)


def main() -> None:
    text = load_assets_text()
    tiles = grab(text, "tile_rom", 4096)
    sprites = grab(text, "sprite_rom", 4096)
    color = grab(text, "color_prom", 32)
    palette = grab(text, "palette_prom", 256)

    chars = bytearray(0x8000)  # 1024 tiles
    for t in range(256):
        chars[t * 32 : (t + 1) * 32] = encode_char_tile(decode_tile(tiles[t * 16 : t * 16 + 16]))

    spr = bytearray(0x4000)  # 256 sprites
    for s in range(64):
        spr[s * 64 : (s + 1) * 64] = encode_sprite(decode_sprite(sprites[s * 64 : s * 64 + 64]))

    proms = build_proms(color, palette)

    out = []
    out.append("/* Auto-generated by scripts/gen_pcktgal_pacman_gfx.py — do not edit. */")
    out.append('#include "pacman_common.h"')
    out.append("")
    out.append('#pragma rodata-name (push, "CHARS")')
    out.append(c_array("TILE_ROM", bytes(chars), "{w:8,h:8,bpp:1,np:4,pofs:8,wpimg:32,brev:1,count:256}"))
    out.append("#pragma rodata-name (pop)")
    out.append("")
    out.append('#pragma rodata-name (push, "SPRITES")')
    out.append(c_array("SPRITE_ROM", bytes(spr), "{w:16,h:16,bpp:1,np:2,pofs:32,wpimg:64,brev:1,count:64}"))
    out.append("#pragma rodata-name (pop)")
    out.append("")
    out.append('#pragma rodata-name (push, "PROMS")')
    out.append(c_array("COLOR_PROMS", proms, '{pal:"nes",n:512}'))
    out.append("#pragma rodata-name (pop)")
    out.append("")
    out.append("/* Keep segments linked */")
    out.append("void pacman_gfx_keep(void) {")
    out.append("  (void)TILE_ROM;")
    out.append("  (void)SPRITE_ROM;")
    out.append("  (void)COLOR_PROMS;")
    out.append("}")
    OUT.write_text("\n".join(out) + "\n")
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
