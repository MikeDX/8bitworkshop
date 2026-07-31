#!/usr/bin/env python3
"""Generate Pac-Man arcade assets + relocatable Namco sound driver.

  python3 scripts/gen_pacman_assets.py

Writes:
  presets/pacman/pacman_assets.c / .h   — tiles, sprites, PROMs, maze
  presets/pacman/pacman_sound.c         — ROM sound engine as relocatable ASM

The sound engine is no longer `__at(0x2CC1)`. Absolute engine/table refs are
rewritten to `_pac_sound_engine+off` / `_pac_sound_tables+off` so the linker
places it with the rest of `_CODE`. NMI must `call _pac_sound_effects` then
`call _pac_sound_engine` (see pacman.c).
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PAC = ROOT / "pacman"
OUT_C = ROOT / "presets" / "pacman" / "pacman_assets.c"
OUT_H = ROOT / "presets" / "pacman" / "pacman_assets.h"
OUT_SND = ROOT / "presets" / "pacman" / "pacman_sound.c"

ENG0, ENG1 = 0x2CC1, 0x2FBA
TAB0, TAB1 = 0x3B30, 0x3CDE
EFFECTS_OFF = 0x2D0C - ENG0  # secondary NMI entry

MAZE_ASCII = [
    "0UUUUUUUUUUUU45UUUUUUUUUUUU1",
    "L............rl............R",
    "L.ebbf.ebbbf.rl.ebbbf.ebbf.R",
    "LPr  l.r   l.rl.r   l.r  lPR",
    "L.guuh.guuuh.gh.guuuh.guuh.R",
    "L..........................R",
    "L.ebbf.ef.ebbbbbbf.ef.ebbf.R",
    "L.guuh.rl.guuyxuuh.rl.guuh.R",
    "L......rl....rl....rl......R",
    "2BBBBf.rzbbf rl ebbwl.eBBBB3",
    "     L.rxuuh gh guuyl.R     ",
    "     L.rl          rl.R     ",
    "     L.rl mjs--tjn rl.R     ",
    "UUUUUh.gh i      q gh.gUUUUU",
    "      .   i      q   .      ",
    "BBBBBf.ef i      q ef.eBBBBB",
    "     L.rl okkkkkkp rl.R     ",
    "     L.rl          rl.R     ",
    "     L.rl ebbbbbbf rl.R     ",
    "0UUUUh.gh guuyxuuh gh.gUUUU1",
    "L............rl............R",
    "L.ebbf.ebbbf.rl.ebbbf.ebbf.R",
    "L.guyl.guuuh.gh.guuuh.rxuh.R",
    "LP..rl.......  .......rl..PR",
    "6bf.rl.ef.ebbbbbbf.ef.rl.eb8",
    "7uh.gh.rl.guuyxuuh.rl.gh.gu9",
    "L......rl....rl....rl......R",
    "L.ebbbbwzbbf.rl.ebbwzbbbbf.R",
    "L.guuuuuuuuh.gh.guuuuuuuuh.R",
    "L..........................R",
    "2BBBBBBBBBBBBBBBBBBBBBBBBBB3",
]

ASCII_TO_TILE = {
    " ": 0x40,
    ".": 0x10,
    "P": 0x14,
    "-": 0xCF,
    "0": 0xD1,
    "1": 0xD0,
    "2": 0xD5,
    "3": 0xD4,
    "4": 0xFB,
    "5": 0xFA,
    "6": 0xD7,
    "7": 0xD9,
    "8": 0xD6,
    "9": 0xD8,
    "U": 0xDB,
    "L": 0xD3,
    "R": 0xD2,
    "B": 0xDC,
    "b": 0xDF,
    "e": 0xE7,
    "f": 0xE6,
    "g": 0xEB,
    "h": 0xEA,
    "l": 0xE8,
    "r": 0xE9,
    "u": 0xE5,
    "w": 0xF5,
    "x": 0xF2,
    "y": 0xF3,
    "z": 0xF4,
    "m": 0xED,
    "n": 0xEC,
    "o": 0xEF,
    "p": 0xEE,
    "j": 0xDD,
    "i": 0xD2,
    "k": 0xDB,
    "q": 0xD3,
    "s": 0xF1,
    "t": 0xF0,
}


def c_array(name: str, addr: str | None, data: bytes, meta: str = "") -> str:
    if addr:
        hdr = f"const byte __at({addr}) {name}[{len(data)}] = "
    else:
        hdr = f"const byte {name}[{len(data)}] = "
    if meta:
        hdr += meta + " "
    lines = [hdr + "{\n"]
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        lines.append("  " + ",".join(f"0x{b:02x}" for b in chunk) + ",\n")
    lines.append("};\n")
    return "".join(lines)


def maze_tiles_bytes() -> bytes:
    assert len(MAZE_ASCII) == 31
    bad = [i for i, r in enumerate(MAZE_ASCII) if len(r) != 28]
    if bad:
        raise SystemExit(f"maze rows need 28 cols: {[(i, len(MAZE_ASCII[i])) for i in bad]}")
    out = bytearray()
    for row in MAZE_ASCII:
        for c in row:
            out.append(ASCII_TO_TILE.get(c, 0x10))
    return bytes(out)


def _dd_len(op2: int) -> int:
    if op2 == 0xCB:
        return 4
    if op2 in (0x21, 0x22, 0x2A):
        return 4
    indexed3 = {
        0x34, 0x35, 0x36, 0x46, 0x4E, 0x56, 0x5E, 0x66, 0x6E,
        0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x77, 0x7E,
        0x86, 0x8E, 0x96, 0x9E, 0xA6, 0xAE, 0xB6, 0xBE,
    }
    if op2 in indexed3:
        return 3
    return 2


def engine_reloc_sites(engine: bytes) -> dict[int, tuple[str, int]]:
    """Map byte offset of abs operand lo-byte → ('eng'|'tab', original_addr)."""
    lengths: dict[int, int | None] = {}
    for b in range(0x40):
        lengths[b] = 1
    for b, ln in [
        (0x01, 3), (0x06, 2), (0x0E, 2), (0x10, 2), (0x11, 3), (0x16, 2), (0x18, 2),
        (0x1E, 2), (0x20, 2), (0x21, 3), (0x22, 3), (0x26, 2), (0x28, 2), (0x2A, 3),
        (0x2E, 2), (0x30, 2), (0x31, 3), (0x32, 3), (0x36, 2), (0x38, 2), (0x3A, 3),
        (0x3E, 2),
    ]:
        lengths[b] = ln
    for b in range(0x40, 0xC0):
        lengths[b] = 1
    for b, ln in [
        (0xC0, 1), (0xC1, 1), (0xC2, 3), (0xC3, 3), (0xC4, 3), (0xC5, 1), (0xC6, 2),
        (0xC7, 1), (0xC8, 1), (0xC9, 1), (0xCA, 3), (0xCB, 2), (0xCC, 3), (0xCD, 3),
        (0xCE, 2), (0xCF, 1), (0xD0, 1), (0xD1, 1), (0xD2, 3), (0xD3, 2), (0xD4, 3),
        (0xD5, 1), (0xD6, 2), (0xD7, 1), (0xD8, 1), (0xD9, 1), (0xDA, 3), (0xDB, 2),
        (0xDC, 3), (0xDD, None), (0xDE, 2), (0xDF, 1), (0xE0, 1), (0xE1, 1), (0xE2, 3),
        (0xE3, 1), (0xE4, 3), (0xE5, 1), (0xE6, 2), (0xE7, 1), (0xE8, 1), (0xE9, 1),
        (0xEA, 3), (0xEB, 1), (0xEC, 3), (0xED, None), (0xEE, 2), (0xEF, 1), (0xF0, 1),
        (0xF1, 1), (0xF2, 3), (0xF3, 1), (0xF4, 3), (0xF5, 1), (0xF6, 2), (0xF7, 1),
        (0xF8, 1), (0xF9, 1), (0xFA, 3), (0xFB, 1), (0xFC, 3), (0xFD, None), (0xFE, 2),
        (0xFF, 1),
    ]:
        lengths[b] = ln

    ed_abs = {0x43, 0x4B, 0x53, 0x5B, 0x63, 0x6B, 0x73, 0x7B}
    sites: dict[int, tuple[str, int]] = {}

    def classify(addr: int) -> str | None:
        if ENG0 <= addr < ENG1:
            return "eng"
        if TAB0 <= addr < TAB1:
            return "tab"
        return None

    def add(off: int, addr: int) -> None:
        kind = classify(addr)
        if kind:
            sites[off] = (kind, addr)

    i = 0
    n = len(engine)
    while i < n:
        op = engine[i]
        if op in (0xDD, 0xFD):
            if i + 1 >= n:
                break
            op2 = engine[i + 1]
            ln = _dd_len(op2)
            if op2 in (0x21, 0x22, 0x2A) and i + 4 <= n:
                addr = engine[i + 2] | (engine[i + 3] << 8)
                add(i + 2, addr)
            i += ln
            continue
        if op == 0xED:
            if i + 1 >= n:
                break
            op2 = engine[i + 1]
            if op2 in ed_abs and i + 4 <= n:
                addr = engine[i + 2] | (engine[i + 3] << 8)
                add(i + 2, addr)
                i += 4
            else:
                i += 2
            continue
        if op == 0xCB:
            i += 2
            continue
        if op == 0xE7:
            # RST 20 jump table: always 16 words (index = A & 0x0F).
            for t in range(16):
                j = i + 1 + t * 2
                if j + 1 >= n:
                    break
                addr = engine[j] | (engine[j + 1] << 8)
                add(j, addr)
            i += 1 + 16 * 2
            continue
        ln = lengths.get(op, 1)
        if ln is None:
            i += 1
            continue
        if ln == 3 and i + 3 <= n:
            addr = engine[i + 1] | (engine[i + 2] << 8)
            add(i + 1, addr)
        i += ln
    return sites


def table_reloc_sites(tables: bytes) -> dict[int, tuple[str, int]]:
    sites: dict[int, tuple[str, int]] = {}
    for j in range(0, len(tables) - 1, 2):
        addr = tables[j] | (tables[j + 1] << 8)
        if TAB0 <= addr < TAB1:
            sites[j] = ("tab", addr)
        elif ENG0 <= addr < ENG1:
            sites[j] = ("eng", addr)
    return sites


def emit_asm_blob(
    data: bytes,
    sites: dict[int, tuple[str, int]],
    mid_labels: dict[int, str] | None = None,
    start_label: str | None = None,
) -> str:
    """Emit sdasz80 .db/.dw for a relocatable blob."""
    mid_labels = mid_labels or {}
    lines: list[str] = []
    if start_label:
        lines.append(f"{start_label}::\n")
    i = 0
    pending: list[int] = []

    def flush_db() -> None:
        nonlocal pending
        if not pending:
            return
        for base in range(0, len(pending), 16):
            chunk = pending[base : base + 16]
            lines.append("        .db " + ", ".join(f"0x{b:02x}" for b in chunk) + "\n")
        pending = []

    while i < len(data):
        if i in mid_labels:
            flush_db()
            lines.append(f"{mid_labels[i]}::\n")
        if i in sites:
            flush_db()
            kind, addr = sites[i]
            if kind == "eng":
                lines.append(f"        .dw _pac_sound_engine + 0x{addr - ENG0:x}\n")
            else:
                lines.append(f"        .dw _pac_sound_tables + 0x{addr - TAB0:x}\n")
            i += 2
            continue
        pending.append(data[i])
        i += 1
    flush_db()
    return "".join(lines)


def gen_sound_c(engine: bytes, tables: bytes) -> str:
    eng_sites = engine_reloc_sites(engine)
    tab_sites = table_reloc_sites(tables)
    # SDCC already emits _pac_sound_engine:: for the C function — do not redefine.
    eng_asm = emit_asm_blob(
        engine,
        eng_sites,
        mid_labels={EFFECTS_OFF: "_pac_sound_effects"},
    )
    # Ensure NMI can call the mid-engine entry from another translation unit.
    eng_asm = eng_asm.replace(
        "_pac_sound_effects::\n",
        ".globl _pac_sound_effects\n_pac_sound_effects::\n",
        1,
    )
    tab_asm = emit_asm_blob(tables, tab_sites, start_label="_pac_sound_tables")
    tab_asm = ".globl _pac_sound_tables\n" + tab_asm
    return f"""/* Auto-generated by scripts/gen_pacman_assets.py — do not edit.
 * Namco Pac-Man WSG sound engine + tables, relocatable in _CODE.
 * NMI: call _pac_sound_effects ; call _pac_sound_engine
 */
#include "pacman_common.h"

/* Entry was ROM 0x2CC1; effects label at +0x{EFFECTS_OFF:x} (was 0x2D0C).
 * Tables follow immediately (were ROM 0x3B30). */
void pac_sound_engine(void) __naked {{
__asm
{eng_asm}{tab_asm}__endasm;
}}
"""


def main() -> None:
    tiles = (PAC / "pacman.5e").read_bytes()
    sprites = (PAC / "pacman.5f").read_bytes()
    color = (PAC / "82s123.7f").read_bytes()
    pal = (PAC / "82s126.4a").read_bytes()
    prog = b"".join((PAC / f"pacman.{x}").read_bytes() for x in ("6e", "6f", "6h", "6j"))

    wave_path = PAC / "82s126.1m"
    if wave_path.exists():
        waves = wave_path.read_bytes()
    else:
        hello = (ROOT / "presets" / "pacman" / "hello.c").read_text()
        start = hello.index("wave_rom[256]")
        brace = hello.index("{", start)
        end = hello.index("};", brace)
        waves = bytes(
            int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", hello[brace + 1 : end])
        )

    rst = prog[0x0010:0x0028]
    engine = bytes(prog[ENG0:ENG1])
    tables = bytes(prog[TAB0:TAB1])

    entries = [1, 3, 5, 7, 9, 0x10, 0x11, 17, 18, 25, 0]
    editor = bytearray()
    for e in entries:
        for p in pal[e * 4 : e * 4 + 4]:
            editor.append(color[p & 31])

    rst_hex = ",".join(f"0x{b:02x}" for b in rst)
    eng_sites = engine_reloc_sites(engine)
    tab_sites = table_reloc_sites(tables)

    hdr = f"""/* Auto-generated by scripts/gen_pacman_assets.py — do not edit.
 * Arcade Pac-Man tile/sprite/PROM assets (externs).
 * Link: //#link \"pacman_assets.c\"  and  //#link \"pacman_sound.c\"
 *
 * Sound driver is relocatable ASM in pacman_sound.c (not fixed __at).
 * Maze tiles are normal CONST.
 */
#ifndef PACMAN_ASSETS_H
#define PACMAN_ASSETS_H

#include \"pacman_common.h\"

/* RST 10/18/20 from pacman.6e — .db these at 0x0010 in start():
 * {rst_hex}
 */

void pac_sound_engine(void);
/* NMI secondary entry (ROM 0x2D0C); label inside pac_sound_engine. */
void pac_sound_effects(void);

extern const byte pac_sound_tables[];
extern const byte maze_tiles[868];
extern const byte color_prom[32];
extern const byte palette_prom[256];
extern const byte wave_rom[256];
extern const byte tile_rom[4096];
extern const byte sprite_rom[4096];

#endif
"""

    maze = maze_tiles_bytes()
    body = [
        "/* Auto-generated by scripts/gen_pacman_assets.py — do not edit. */\n",
        '#include "pacman_common.h"\n\n',
        c_array(
            "maze_tiles",
            None,
            maze,
            "/* 31*28 Namco tile indexes */",
        ),
        "\n/* Arcade gfx */\n",
        c_array("color_prom", "0x6000", color, '/*{pal:"pacman",n:32}*/'),
        "\n",
        c_array("editor_pals", None, bytes(editor), '/*{pal:"pacman",layout:"pacman"}*/'),
        "\n",
        c_array("palette_prom", "0x6100", pal, ""),
        "\n",
        c_array("wave_rom", "0x6200", waves, "/*{w:32,h:1,count:8,bpp:8}*/"),
        "\n",
        c_array("tile_rom", "0x4000", tiles, "/*{w:8,h:8,count:256,bpp:2,pacstrip:1}*/"),
        "\n",
        c_array("sprite_rom", "0x5000", sprites, "/*{w:16,h:16,count:64,bpp:2,pacstrip:1}*/"),
    ]

    OUT_H.write_text(hdr)
    OUT_C.write_text("".join(body))
    OUT_SND.write_text(gen_sound_c(engine, tables))
    print(f"wrote {OUT_H.relative_to(ROOT)} ({OUT_H.stat().st_size} bytes)")
    print(f"wrote {OUT_C.relative_to(ROOT)} ({OUT_C.stat().st_size} bytes)")
    print(f"wrote {OUT_SND.relative_to(ROOT)} ({OUT_SND.stat().st_size} bytes)")
    print(
        f"engine reloc sites={len(eng_sites)} table reloc sites={len(tab_sites)} "
        f"effects off=0x{EFFECTS_OFF:x}"
    )


if __name__ == "__main__":
    main()
