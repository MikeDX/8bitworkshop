/*
 * Midway MCR 91490 CPU + 91464 sprite board helpers for 8bitworkshop.
 * Aligned with MAME `timber` (cpu_91490_map / mcr_91490).
 */
#ifndef MCR_H
#define MCR_H

typedef unsigned char byte;
typedef unsigned short word;
typedef signed char sbyte;

/* Work RAM / video — matches MAME cpu_91490_map */
byte __at (0xe000) nvram[0x800];
byte __at (0xe800) sprram[0x200];
byte __at (0xf000) vram[0x800];
byte __at (0xf800) palram[0x80];

/* SSIO inputs — active LOW like real hardware / MAME timber */
__sfr __at (0x00) INPUT0;
__sfr __at (0x01) INPUT1;
__sfr __at (0x02) INPUT2;
__sfr __at (0x03) DIPSW;

__sfr __at (0xe0) WATCHDOG_PORT;
__sfr __at (0xf0) CTC0;
__sfr __at (0xf1) CTC1;
__sfr __at (0xf2) CTC2;
__sfr __at (0xf3) CTC3;

/*
 * Homebrew AY access via SSIO command latches (MAME 0x1c-0x1f).
 * 8bw maps these straight to dual AY-3-8910 chips. Stock timber SSIO
 * ROMs ignore them — replace SSIO firmware later for real hardware audio.
 */
__sfr __at (0x1c) AY1_REG;
__sfr __at (0x1d) AY1_DATA;
__sfr __at (0x1e) AY2_REG;
__sfr __at (0x1f) AY2_DATA;

#define WATCHDOG()  do { WATCHDOG_PORT = 0; } while (0)

inline void mcr_ay1(byte reg, byte data) {
  AY1_REG = reg;
  AY1_DATA = data;
}
inline void mcr_ay2(byte reg, byte data) {
  AY2_REG = reg;
  AY2_DATA = data;
}

/* timber IP0 */
#define COIN1   (!(INPUT0 & 0x01))
#define START1  (!(INPUT0 & 0x04))
#define START2  (!(INPUT0 & 0x08))
/* timber IP1: Right, Left, Down, Up, B1, B2 */
#define RIGHT1  (!(INPUT1 & 0x01))
#define LEFT1   (!(INPUT1 & 0x02))
#define DOWN1   (!(INPUT1 & 0x04))
#define UP1     (!(INPUT1 & 0x08))
#define FIRE1   (!(INPUT1 & 0x10))
#define FIRE2   (!(INPUT1 & 0x20))

/* VBlank flag set by ISR (IM2 via CTC daisy chain). Defined in each preset .c. */
extern volatile byte mcr_vblank_flag;
void mcr_vblank_isr(void);

/*
 * Enable 30Hz VBlank IRQ via CTC CH3 (MAME pulses TRG3 once/frame).
 * Uses IM2 + vector page 0x7F (table at 0x7F00 → mcr_vblank_isr).
 * 8bw fires IRQ every advanceFrame once the CTC interrupt bit is set.
 */
inline void mcr_enable_vblank_irq(void) {
  mcr_vblank_flag = 0;
  CTC0 = 0x00;   /* interrupt vector base (low byte) */
  CTC3 = 0xD7;   /* int + counter + time-const follows + control */
  CTC3 = 0x01;   /* downcount 1 external pulse → IRQ */
__asm
  ld   a, #0x7F
  ld   i, a
  im   2
  ei
__endasm;
}

inline void wait_vblank(void) {
  WATCHDOG();
  mcr_vblank_flag = 0;
  while (!mcr_vblank_flag) WATCHDOG();
}

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
  set_sprite_hw(i, 0, 0, 0, 0);
}

inline void hide_all_sprites(void) {
  byte i;
  for (i = 0; i < 32; i++) hide_sprite(i);
}

/*
 * Palette write (MAME mcr_paletteram9_w):
 *   color = data | ((offset & 1) << 8)
 *   R = bits[8:6], G = bits[2:0], B = bits[5:3]
 * R[2] is A0 of the write — one store to even (r2=0) or odd (r2=1).
 */
inline void set_color(byte index, byte r3, byte g3, byte b3) {
  byte i = index & 63;
  byte data = (byte)((g3 & 7) | ((b3 & 7) << 3) | ((r3 & 3) << 6));
  if (r3 & 4)
    palram[i * 2 + 1] = data;
  else
    palram[i * 2] = data;
}

inline void mcr_init(void) {
  byte i;
  WATCHDOG();
  for (i = 0; i < 64; i++) set_color(i, 0, 0, 0);
  for (i = 0; i < 32; i++) hide_sprite(i);
}

#endif
