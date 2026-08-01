#!/usr/bin/env python3
"""
Generate presets/pengo/hello.c graphics (homebrew font + PROMs).

Combined ROM offsets (see src/machine/pengo.ts):
  0x8000 tile bank 0, 0x9000 tile bank 1,
  0xA000 sprite bank 0, 0xB000 sprite bank 1,
  0xC000 color PROM, 0xC100 lookup (1024), 0xC500 wave

Usage:
  python3 scripts/gen_pengo_gfx.py
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "presets" / "pengo" / "hello.c"

GFX_BEGIN = "/* ==== PENGO GFX BEGIN (regenerate: scripts/gen_pengo_gfx.py) ==== */"
GFX_END = "/* ==== PENGO GFX END ==== */"


def encode_strip(pixels, width, bx, by):
    out = [0] * 8
    for x in range(8):
        strip = 0
        for y in range(4):
            i = (3 - y) * width + (7 - x)
            pen = pixels[by * width + bx + i] & 3
            if pen & 1:
                strip |= 1 << y
            if pen & 2:
                strip |= 1 << (y + 4)
        out[x] = strip
    return out


def encode_tile(pix8):
    return encode_strip(pix8, 8, 0, 4) + encode_strip(pix8, 8, 0, 0)


def encode_sprite(pix16):
    layouts = [
        (8, 12), (8, 0), (8, 4), (8, 8),
        (0, 12), (0, 0), (0, 4), (0, 8),
    ]
    out = []
    for bx, by in layouts:
        out.extend(encode_strip(pix16, 16, bx, by))
    return out


def pix8(rows):
    m = {'.': 0, ' ': 0, '0': 0, '1': 1, '2': 2, '3': 3, '#': 3, '*': 2, '+': 1}
    out = []
    for r in rows:
        assert len(r) == 8, r
        out.extend(m[c] for c in r)
    return out


def pix16(rows):
    m = {'.': 0, ' ': 0, '0': 0, '1': 1, '2': 2, '3': 3, '#': 3, '*': 2, '+': 1}
    out = []
    for r in rows:
        assert len(r) == 16, (len(r), r)
        out.extend(m[c] for c in r)
    return out


def c_array(name, at, data, tag="", per_line=16):
    lines = []
    for i in range(0, len(data), per_line):
        chunk = data[i : i + per_line]
        lines.append("  " + ",".join(f"0x{b:02x}" for b in chunk) + ",")
    body = "\n".join(lines)
    comment = f" /*{tag}*/" if tag else ""
    return f"const byte __at(0x{at:04x}) {name}[{len(data)}] ={comment} {{\n{body}\n}};\n"


# Minimal 8x8 glyphs (space + digits + A-Z style block font)
def glyph_rows(ch):
    # Very small set: filled patterns for demo readability
    blank = ["........"] * 8
    if ch == " ":
        return blank
    # Digits / letters: simple block outlines
    patterns = {
        "0": ["######", "#....#", "#....#", "#....#", "#....#", "#....#", "######", "......"],
        "1": ["..##..", ".###..", "..##..", "..##..", "..##..", "..##..", "######", "......"],
        "A": [".####.", "#....#", "#....#", "######", "#....#", "#....#", "#....#", "......"],
        "E": ["######", "#.....", "#.....", "#####.", "#.....", "#.....", "######", "......"],
        "G": [".####.", "#....#", "#.....", "#..###", "#....#", "#....#", ".####.", "......"],
        "H": ["#....#", "#....#", "#....#", "######", "#....#", "#....#", "#....#", "......"],
        "L": ["#.....", "#.....", "#.....", "#.....", "#.....", "#.....", "######", "......"],
        "N": ["#....#", "##...#", "#.#..#", "#..#.#", "#...##", "#....#", "#....#", "......"],
        "O": [".####.", "#....#", "#....#", "#....#", "#....#", "#....#", ".####.", "......"],
        "P": ["#####.", "#....#", "#....#", "#####.", "#.....", "#.....", "#.....", "......"],
        "R": ["#####.", "#....#", "#....#", "#####.", "#..#..", "#...#.", "#....#", "......"],
        "W": ["#....#", "#....#", "#....#", "#.#..#", "#.#.#.", "##.##.", "#....#", "......"],
        "Y": ["#....#", "#....#", ".#..#.", "..##..", "..##..", "..##..", "..##..", "......"],
        "!": ["..##..", "..##..", "..##..", "..##..", "......", "......", "..##..", "......"],
    }
    rows = patterns.get(ch, ["######", "#....#", "#....#", "#....#", "#....#", "#....#", "######", "......"])
    # pad to 8 cols
    return [r.ljust(8, ".")[:8] for r in rows]


def make_font_tiles():
    tiles = [0] * (256 * 16)
    mapping = {}
    for code in range(256):
        ch = chr(code) if 32 <= code < 127 else " "
        if "0" <= ch <= "9" or "A" <= ch <= "Z" or ch in " !":
            enc = encode_tile(pix8(glyph_rows(ch if ch != " " else " ")))
        elif code == 0x40:  # space / blank
            enc = encode_tile(pix8(glyph_rows(" ")))
        elif code == 0x10:  # dot
            enc = encode_tile(pix8([
                "........", "........", "...##...", "..####..", "..####..", "...##...", "........", "........",
            ]))
        elif code == 0x14:  # power
            enc = encode_tile(pix8([
                "...##...", "..####..", ".######.", "########", "########", ".######.", "..####..", "...##...",
            ]))
        else:
            enc = encode_tile(pix8(glyph_rows(" ")))
        tiles[code * 16 : code * 16 + 16] = enc
        mapping[code] = ch
    return tiles


def make_sprites():
    sprites = [0] * (64 * 64)
    player = encode_sprite(pix16([
        "......####......",
        "....########....",
        "...##########...",
        "..############..",
        "..###..##..###..",
        ".##############.",
        ".##############.",
        ".######..######.",
        ".######..######.",
        ".##############.",
        ".##############.",
        "..############..",
        "..############..",
        "...##########...",
        "....########....",
        "......####......",
    ]))
    alien = encode_sprite(pix16([
        "......####......",
        "....########....",
        "...##.####.##...",
        "..############..",
        ".###.##..##.###.",
        ".##############.",
        ".##.########.##.",
        ".##...####...##.",
        "..##........##..",
        "...##......##...",
        "....##....##....",
        ".....##..##.....",
        "......####......",
        "......#..#......",
        ".....##..##.....",
        "....##....##....",
    ]))
    sprites[0:64] = player
    sprites[64:128] = alien
    return sprites


def make_color_prom():
    # Pac-Man-ish resistor PROM (32 entries); good enough for hello
    return [
        0x00, 0x07, 0x66, 0xef, 0x00, 0xf8, 0xea, 0x6f, 0x00, 0x3f, 0x00, 0xc9, 0x38, 0xaa, 0xaf, 0xf6,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ]


def make_lookup():
    # 1024 bytes; first 256 define 64 palettes × 4 pens (low nibble used)
    lut = [0] * 0x400
    pals = [
        [0x00, 0x00, 0x00, 0x00],
        [0x00, 0x07, 0x66, 0xef],
        [0x00, 0xf8, 0xea, 0x6f],
        [0x00, 0x3f, 0xc9, 0xf6],
        [0x00, 0x00, 0x00, 0x00],
        [0x00, 0xaf, 0xc9, 0xf6],  # cyan-ish as 5
        [0x00, 0x00, 0x00, 0x00],
        [0x00, 0x38, 0xaa, 0xf6],  # orange-ish as 7
        [0x00, 0x00, 0x00, 0x00],
        [0x00, 0xf6, 0xc9, 0x07],  # yellow as 9
    ]
    for i, p in enumerate(pals):
        lut[i * 4 : i * 4 + 4] = p
    # fill remaining with identity-ish
    for i in range(len(pals), 64):
        lut[i * 4] = 0
        lut[i * 4 + 1] = (i & 15)
        lut[i * 4 + 2] = ((i * 3) & 15)
        lut[i * 4 + 3] = 15
    return lut


def make_waves():
    waves = []
    for s in range(32):
        waves.append(int(8 + 7.5 * __import__("math").sin(s / 32 * 3.14159265 * 2)) & 15)
    for s in range(32):
        waves.append(15 if s < 16 else 0)
    waves.extend([0] * (256 - len(waves)))
    return waves[:256]


HELLO_SRC = r'''/*
 * Pengo hardware "Hello" example for 8bitworkshop
 *
 * 32KB program space, remapped VRAM/RAM vs Pac-Man, dual gfx banks.
 */
//#link "pengo_common.c"
#include "pengo_common.h"

void main(void) {
  byte x, y, frame;

  x = 10;
  y = 20;
  frame = 0;
  video_framecount = 0;

  pengo_irq_enable();
  sound_enable = 1;
  flip_screen = 0;
  gfx_bank = 0;
  palette_bank = 0;
  colortable_bank = 0;
  watchdog = 0;

  clrscr(0);
  hide_all_sprites();

  put_string(4, 4, "HELLO PENGO", PAL_YELLOW);
  put_string(4, 6, "32K ROM SPACE", PAL_CYAN);

  poke_tile(4, 8, T_DOT, PAL_ORANGE);
  poke_tile(6, 8, T_POWER, PAL_ORANGE);

  set_sprite(0, S_PLAYER, PAL_YELLOW, 96, 120);
  set_sprite(1, S_ALIEN, PAL_PINK, 128, 120);

  while (1) {
    wait_vblank();
    frame++;

    poke_tile(x, y, T_BLANK, 0);

    if (UP1 && y > 2) y--;
    if (DOWN1 && y < 33) y++;
    if (LEFT1 && x > 0) x--;
    if (RIGHT1 && x < 27) x++;

    poke_tile(x, y, T_CIRCLE, PAL_YELLOW);

    poke_tile(2, 30, (byte)('0' + (frame & 7)), PAL_CYAN);
    poke_tile(24, 30, UP1    ? 'U' : T_BLANK, PAL_PINK);
    poke_tile(24, 31, RIGHT1 ? 'R' : T_BLANK, PAL_PINK);
    poke_tile(24, 32, DOWN1  ? 'D' : T_BLANK, PAL_PINK);
    poke_tile(24, 33, LEFT1  ? 'L' : T_BLANK, PAL_PINK);

    if (START1) put_string(2, 32, "START", PAL_CYAN);
    if (COIN1)  put_string(2, 34, "COIN", PAL_ORANGE);
    if (FIRE1)  put_string(2, 33, "FIRE", PAL_PINK);

    set_sprite(0, S_PLAYER, PAL_YELLOW, (byte)(x * 8), (byte)(y * 8));
  }
}

'''


def main():
    tiles0 = make_font_tiles()
    tiles1 = [0] * (256 * 16)
    sprites0 = make_sprites()
    sprites1 = [0] * (64 * 64)
    color = make_color_prom()
    lookup = make_lookup()
    waves = make_waves()

    parts = [
        HELLO_SRC.rstrip() + "\n\n",
        GFX_BEGIN + "\n",
        c_array("tile_rom0", 0x8000, tiles0, 'bank:0'),
        c_array("tile_rom1", 0x9000, tiles1, 'bank:1'),
        c_array("sprite_rom0", 0xA000, sprites0, 'bank:0'),
        c_array("sprite_rom1", 0xB000, sprites1, 'bank:1'),
        c_array("color_prom", 0xC000, color, 'pal:"pengo",n:32'),
        c_array("lookup_prom", 0xC100, lookup),
        c_array("wave_rom", 0xC500, waves),
        GFX_END + "\n",
    ]
    OUT.write_text("".join(parts))
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
