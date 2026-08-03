# Data East Pocket Gal (`pcktgal`) — Board Capabilities

Notes for homebrew on the Pocket Gal / Pocket Gal 2 hardware, and for evaluating a port of the Pac-Man clone from the `pacmanonpacman` branch.

MAME driver: [`pcktgal.cpp`](https://github.com/mamedev/mame/blob/master/src/mame/dataeast/pcktgal.cpp)  
Tilemap ASIC: DECO BAC06 (`decbac06.cpp`)  
8bitworkshop platform: `?platform=pcktgal`  
Desktop validation: `pcktgal2` via `scripts/build_pcktgal_local.sh` + `scripts/8bw_pcktgal_to_mame.py`

Treat **MAME `pcktgal2`** as the source of truth for video/scroll behavior.

---

## 1. Machine overview

| Item | Detail |
|------|--------|
| Main CPU | MOS 6502 @ **2 MHz** |
| Audio CPU | 6502 @ 1.5 MHz (`pcktgal2`); original `pcktgal` uses encrypted **DECO 222** |
| Prefer for homebrew | **`pcktgal2`** (plain audio 6502, no decryption) |
| Interrupt | **NMI** on VBlank (main CPU) |
| Orientation (stock) | Landscape, MAME `ROT0` |
| Year / maker | 1987–89 Data East (Pocket Gal / Pocket Gal 2 / Super Pool III) |

Stock game is pool/billiards UI — horizontal monitor. A Pac-Man-style port would typically **mount the monitor on its side** (see §8).

---

## 2. Memory map (main CPU)

| Range | Size | Function |
|-------|------|----------|
| `$0000–$07FF` | 2 KB | Work RAM (ZP/stack in low page; cc65 uses `$0200–$07FF`) |
| `$0800–$0FFF` | 2 KB | BAC06 playfield VRAM (1024 big-endian tile **words**) |
| `$1000–$11FF` | 512 B | Sprite RAM (64 sprites × 4 bytes) |
| `$1800` | — | P1 inputs (R); BAC06 ctrlreg (W, `$1800–$1807`) |
| `$1810–$181F` | — | BAC06 scroll registers |
| `$1A00` | — | P2 / coins (R); sound latch + audio NMI (W) |
| `$1C00` | — | DSW (R); ROM bank select (W) |
| `$4000–$5FFF` | 8 KB | Banked ROM window 0 |
| `$6000–$7FFF` | 8 KB | Banked ROM window 1 |
| `$8000–$FFFF` | 32 KB | Fixed program ROM |

### Banking

Write `$1C00`:

- bit0 → bank0 base `$0000` or `$4000` of the 64 KB maincpu image  
- bit1 → bank1 base `$2000` or `$6000`

Fixed `$8000–$FFFF` is always the top 32 KB. Homebrew can keep everything in the fixed half and ignore banking for small games.

### RAM budget (game logic)

Roughly **~1.5 KB** usable BSS/data after ZP/stack (`$0200–$07FF`), plus playfield/sprite buffers that live in VRAM/sprite RAM. Pac-Man’s maze + actors need careful packing (or banked ROM tables).

---

## 3. Video — display

| Item | Value |
|------|--------|
| Raster | 256 × 256 total; **visible 256 × 224** |
| MAME visarea | rows `2×8 .. 30×8−1` (skip 16 lines top/bottom) |
| Refresh | 60 Hz |
| Stock rotation | `ROT0` (wide) |

In the 8bitworkshop emulator, the canvas is **256×224** with the same vertical crop (internal Y = screenY + 16 for sprite math).

---

## 4. Playfield (DECO BAC06)

### Tile format

- **8×8** characters (when ctrl bit0 = 1)  
- **4 bpp** (16 colors per tile)  
- Stock MAME char ROM region is 128 KB / 4096 tiles; **homebrew ships 32 KB / 1024 tiles** in the combined ROM  
- Homebrew planar layout (Asset Editor): 4 planes × 8 bytes, `wpimg:32`, MSB-left (`brev:1`)

### VRAM word (big-endian at `$0800 + index×2`)

```
bits 15–12  colour bank (0–15)  → palette pens 256 + bank×16 + pixel
bits 11–0   tile code
```

CPU window is only **1024 words** (`$0800–$0FFF`). BAC06 internal VRAM is larger; indices outside the CPU window read as **0** (blank).

### Control register (`$1800`, low byte of ctrlreg[0])

| Bit | Meaning (MAME BAC06) |
|-----|----------------------|
| 0 | **1** = 8×8 tiles, **0** = 16×16 |
| 1 | **0** = force `TILE_FLIPX` on all PF tiles; **1** = no forced flip |
| 2–3 | row/column scroll enables (if wired) |
| 7 | flip screen |

Homebrew demos use `POKE(0x1800, 0x03)` — 8×8, no forced flip (assets authored unflipped).

### Tilemap size / scroll (important)

Default dimensions register (ctrlreg[3] = 0) selects BAC06 **mode 0**:

| Mode | Tilemap | Pixel size | Wrap |
|------|---------|------------|------|
| 0 (default) | 128 × 32 | **1024 × 256** | X @ 1024, Y @ 256 |
| 1 | 64 × 64 | 512 × 512 | |
| 2 | 32 × 128 | 256 × 1024 | |

**Horizontal scroll does not wrap at 256.** Only the first 32 columns are filled by CPU writes; scrolling X past 256 shows blank until wrap at 1024.

Scroll registers (`$1810+`, big-endian words): word0 = X, word1 = Y.

Mode-0 tile index (MAME `tile_shape0_8x8_scan`):

```text
index = (col & 0x1f) + ((row & 0x1f) << 5) + ((col & 0x60) << 5)
```

For col 0–31 this matches a simple `row×32+col` map.

---

## 5. Sprites

Software-drawn from sprite RAM (not BAC06). MAME `draw_sprites`:

| Byte | Field |
|------|--------|
| 0 | Y raw; **`$F8` = hide**; screen Y ≈ `240 − raw` |
| 1 | bit0 code[8]; bit1 flipy; bit2 flipx; bits 6–4 colour (0–7) |
| 2 | X raw; screen X ≈ `240 − raw` |
| 3 | code[7:0] |

| Item | Value |
|------|--------|
| Max sprites | **64** (512 bytes / 4) |
| Size | **16×16**, **2 bpp** (4 colors) |
| Palette | pens `colour×4 + pixel` (pens **0–31**) |
| Transparency | pixel 0 |
| Homebrew ROM | 256 sprites × 64 bytes (plane0 then plane1, row-major L/R) |

Coordinates use a bottom-right style origin (same family as many DECO games). Flip bits match MAME. Sprites are drawn over the opaque playfield.

Compared to Pac-Man hardware (**8** sprites): Pocket Gal is generous on sprite count.

---

## 6. Palette (PROMs)

- Two 512-byte PROMs: RG nibbles, then B nibble (MAME resistor weights `0x0e/0x1f/0x43/0x8f`)  
- **512 pens** total  
- Tiles: base **256** + 16×colour + pixel  
- Sprites: pens **0–31** (8 banks × 4)

Not RAM-writable — palette is baked into ROM. Design mazes/sprites around fixed PROM slots (or regenerate PROMs with the Asset Editor / build scripts).

---

## 7. Sound (hardware vs 8bitworkshop)

| Hardware | Pac-Man port |
|----------|----------------|
| YM2203 + YM3812 + MSM5205 ADPCM | **YM2203 SSG** (3 voices: lead/SFX, harmony/fright, siren/eyes). MSM/OPL idle |
| Second 6502 + sound ROM | Homebrew firmware `presets/pcktgal/audio.s` → `eb03-2.f2` |
| Main→audio latch `$1A00` | `play_sfx` / `update_ambient` write command bytes |

Build: `scripts/build_pcktgal_audio.sh` (also run from `build_pcktgal_local.sh pacman`).  
Commands: `presets/pcktgal/audio_cmds.inc` (waka, eat, death, coin, fright, siren, eyes, prelude, intermission).  

SSG voice map (so SFX and ambient can overlap, like Namco’s 3-voice WSG):
- **A** — music lead / one-shot SFX (waka, eat, death, coin, fruit)
- **B** — music harmony / fright warble
- **C** — siren / eyes whoop

Timbre is square-wave SSG, not wavetable — but polyphony matches the arcade role split. Pure MSM5205 ADPCM was abandoned (single stream can’t overlap).

---

## 8. Inputs

Active-low. Ports:

- `$1800` — P1 stick (8-way) + start1/2 + buttons  
- `$1A00` — P2 stick + coin1/2 + buttons  
- `$1C00` — DSW (coinage, flip, lives, etc.)

Enough for a Pac-Man control scheme (4-way can be masked in software).

---

## 9. Homebrew ROM / toolchain

### Combined ld65 image (82 944 bytes)

| Offset | Size | Contents |
|--------|------|----------|
| `$00000` | 32 KB | Program @ CPU `$8000–$FFFF` |
| `$08000` | 32 KB | Char ROM (1024×32) |
| `$10000` | 16 KB | Sprite ROM (256×64) |
| `$14000` | 1 KB | PROMs (512+512) |

cc65: `src/worker/lib/pcktgal/` (`pcktgal.cfg`, `crt0.s`).  
Presets: `hello.c`, `sprites.c`, `spritetest.c`, **`pacman.c`** (port from `pacmanonpacman`).  
Gfx regen for Pac-Man: `python3 scripts/gen_pcktgal_pacman_gfx.py`  
MAME: `scripts/build_pcktgal_local.sh <preset> --run` (exports `pcktgal2` set; char ROM plane order + `init_original` line-swap handled in the converter).

### Pac-Man bring-up status

Port builds and runs under MAME `pcktgal2`. Graphics and draw positions are
**rotated 90° CW** so Pac-Man is vertical (cabinet / `-rol` view). Sound stubbed.
Logical rows y≥32 are clipped (36-tile maze vs 32-tile hardware width).

```bash
scripts/build_pcktgal_local.sh pacman --run
# upright view:
mame pcktgal2 -rompath roms -window -skip_gameinfo -rol
```

---

## 10. Pac-Man port notes (`pacmanonpacman` → `pcktgal`)

### Screen geometry

| | Pac-Man (Namco) | Pocket Gal |
|--|-----------------|------------|
| Logical framebuffer | **224 × 288** (portrait after `ROT90`) | **256 × 224** (landscape) |
| Tile layer | 8×8, **2 bpp**, 36×28-ish maze | 8×8, **4 bpp**, 32×28 visible |
| Sprites | **8 × 16×16**, 2 bpp | **64 × 16×16**, 2 bpp |
| CPU | Z80 | **6502** |
| Palette | Color + lookup PROMs | Dual PROM, fixed 512 pens |

Stock Pac-Man cabinets rotate the monitor. Same idea here:

```text
                    cabinet monitor rotated 90° CW
  PCB raster 256×224  ──────────────────────────►  player sees 224×256
```

### Fit vs Pac-Man maze

Classic maze playfield is **28 × 36 tiles** = **224 × 288** pixels.

| After rotating Pocket Gal’s 256×224 | vs Pac-Man |
|-------------------------------------|------------|
| Width 224 | Matches (28 tiles) |
| Height 256 | **32 px short** of 288 (4 tile rows) |

Options:

1. **Crop / redesign maze** to 28×32 tiles (224×256) — simplest, loses a little tunnel/status space.  
2. **Use BAC06 Y scroll** with a taller virtual map (mode 2 is 256×1024) — possible but only 32 columns are CPU-filled in the usual window; status row handling gets awkward.  
3. **Keep landscape** (no rotate): redraw maze for 32×28 — different game, easier on this board’s native orientation.

Recommendation for a faithful port: **rotated cabinet + 28×32 maze** (or 28×31 + one status row), re-author tiles/sprites for 4bpp PF + 2bpp sprites, keep actor logic from `pacmanonpacman` with a new render backend.

### What ports cleanly

- Tilemap maze + dots (VRAM poke helpers already in demos)  
- Actors as sprites (5+ required; 64 available)  
- Flip bits for left/up facing frames  
- 6502 rewrite of Z80 game loop (logic is already C on `pacmanonpacman`)  
- Desktop parity via MAME `pcktgal2`

### What needs new work

- **Palette PROM** layout for maze / ghosts / fright / fruit  
- **Gfx conversion** from Pac-Man 2bpp strips → Pocket Gal planar chars/sprites (and MAME export remap)  
- **Coordinate system** (Pac-Man sprite origin ≠ DECO `240−x` / `240−y`)  
- **Sound** (no Namco WSG)  
- **Scanline/timing** assumptions if any code depended on Pac-Man’s 288-line frame  
- **Banking** if code+assets exceed 32 KB fixed PRG (likely with full assets)

### Rough feasibility

| Subsystem | Fit |
|-----------|-----|
| Maze + dots | Good (tile budget & VRAM OK; height trim or redesign) |
| Sprites / actors | Good (more sprites than Namco) |
| CPU / RAM | Fair (6502 fine; RAM tight — keep tables in ROM) |
| Scroll / tunnels | Fair (no 256 X-wrap; tunnels are maze tiles, not scroll wrap) |
| Sound | Hard / deferred |
| Pixel-perfect 288p | No — plan for 256p portrait or landscape redesign |

---

## 11. Emulator gaps (8bitworkshop vs MAME)

Implemented and aligned with MAME for homebrew demos:

- 6502 map, NMI, PF draw, sprites, PROM palette  
- BAC06 mode-0 **1024-wide** scroll / tile indexing  
- `TILE_FLIPX` when ctrl bit1 clear  

Still thin / stubbed:

- Audio CPU + YM/MSM  
- Row/column scroll, 16×16 PF mode, flip-screen edge cases  
- Full 4096-tile char ROM (homebrew capped at 1024)  
- Sprite priority exotic cases  

Always re-check video changes under `mame pcktgal2`.

---

## 12. Quick reference — demo helpers

```c
POKE(0x1800, 0x03);           /* 8x8, no forced TILE_FLIPX */
/* scroll X/Y big-endian */
POKE(0x1810, x >> 8); POKE(0x1811, x & 0xff);
POKE(0x1812, y >> 8); POKE(0x1813, y & 0xff);
/* tile at (tx,ty) */
word = (color << 12) | (tile & 0xfff);
POKE(0x0800 + (ty*32u + tx)*2u, word >> 8);
POKE(..., word & 0xff);
/* sprite: hide with Y raw = 0xF8 */
POKE(0x1000 + i*4, 240 - y);
POKE(0x1000 + i*4 + 1, ((color&7)<<4) | (flip&6) | ((code>>8)&1));
POKE(0x1000 + i*4 + 2, 240 - x);
POKE(0x1000 + i*4 + 3, code & 0xff);
```

Validate: `scripts/build_pcktgal_local.sh spritetest --run`
