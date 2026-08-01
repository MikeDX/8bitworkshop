/*
 * Midway MCR-2 (91490 CPU + 91464 sprite board) helpers for 8bitworkshop.
 * Memory map matches src/machine/mcr.ts / src/platform/mcr.ts.
 */
#ifndef MCR_H
#define MCR_H

typedef unsigned char byte;
typedef unsigned short word;
typedef signed char sbyte;

/* Work RAM / video */
byte __at (0xe000) nvram[0x800];
byte __at (0xe800) sprram[0x200];
byte __at (0xf000) vram[0x800];
byte __at (0xf800) palram[0x80];

/* SSIO-style inputs (active high in the 8bw emu) */
__sfr __at (0x00) INPUT0;
__sfr __at (0x01) INPUT1;
__sfr __at (0x02) INPUT2;
__sfr __at (0x03) DIPSW;

__sfr __at (0xe0) WATCHDOG_PORT;
__sfr __at (0xf0) CTC0;
__sfr __at (0xf4) VBLANK_COUNT; /* homebrew: emu frame counter */
/* Homebrew BG scroll (real MCR-2 has none). NES-style: top-left of screen
 * samples nametable at (SCROLL_X, SCROLL_Y). Units = NES pixels (tile grid);
 * emu scales ×2 to the 512×480 canvas. SCROLL_X is signed (e.g. 248 == -8). */
__sfr __at (0xf5) SCROLL_Y;
__sfr __at (0xf6) SCROLL_X;

#define WATCHDOG()  do { WATCHDOG_PORT = 0; } while (0)

/* Busy-wait until the next emulated frame (MCR ~30Hz). */
inline void wait_vblank(void) {
  byte f = VBLANK_COUNT;
  WATCHDOG();
  while (VBLANK_COUNT == f) WATCHDOG();
}

#define COIN1   (INPUT0 & 0x01)
#define START1  (INPUT0 & 0x04)
#define START2  (INPUT0 & 0x08)
#define UP1     (INPUT1 & 0x01)
#define DOWN1   (INPUT1 & 0x02)
#define LEFT1   (INPUT1 & 0x04)
#define RIGHT1  (INPUT1 & 0x08)
#define FIRE1   (INPUT1 & 0x10)
#define FIRE2   (INPUT1 & 0x20)

/* VRAM: 32×30 tiles, 2 bytes each (91490 format) */
#define TILE_COLS 32
#define TILE_ROWS 30

inline void set_tile(byte col, byte row, word code, byte palette, byte flipx, byte flipy) {
  word ofs = (word)(((word)row << 5) + col) << 1;
  vram[ofs] = (byte)(code & 0xff);
  vram[ofs + 1] = (byte)(((code >> 8) & 0x03)
    | (flipx ? 0x04 : 0)
    | (flipy ? 0x08 : 0)
    | ((palette & 0x03) << 4));
}

/* Sprite RAM: 32 sprites × 4 bytes (91464). Y/X are hardware-encoded. */
inline void set_sprite_hw(byte i, byte y, byte attrib, byte code, byte x) {
  byte* p = &sprram[(word)i * 4];
  p[0] = y;
  p[1] = attrib;
  p[2] = code;
  p[3] = x;
}

/*
 * Sprite: screen pixels → 91464 regs (MAME):
 *   sx_pix = (xreg - 3) * 2
 *   sy_pix = (241 - yreg) * 2
 */
inline void set_sprite_xy(byte i, byte code, byte pal, word sx, word sy) {
  byte yreg = (byte)(241 - ((sy >> 1) & 0xff));
  byte xreg = (byte)(((sx >> 1) + 3) & 0xff);
  byte attrib = (byte)(pal & 3);
  if (code & 0x100) attrib |= 0x08;
  set_sprite_hw(i, yreg, attrib, (byte)(code & 0xff), xreg);
}

inline void hide_sprite(byte i) {
  /* Y reg 0 → sy = 482 (off visible 480); keep X clear too */
  set_sprite_hw(i, 0, 0, 0, 0);
}

/* Palette: odd byte holds G[2:0]|B[2:0]|R[1:0]; even bit0 = R MSB */
inline void set_color(byte index, byte r3, byte g3, byte b3) {
  byte i = index & 63;
  palram[i * 2] = (r3 >> 2) & 1;
  palram[i * 2 + 1] = (byte)((g3 & 7) | ((b3 & 7) << 3) | ((r3 & 3) << 6));
}

inline void mcr_init(void) {
  byte i;
  WATCHDOG();
  for (i = 0; i < 64; i++) set_color(i, 0, 0, 0);
  for (i = 0; i < 32; i++) hide_sprite(i);
}

#endif
