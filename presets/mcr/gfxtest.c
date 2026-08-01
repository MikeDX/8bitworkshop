/*
 * Midway MCR (91490 / timber) — graphics test.
 * Tile grid across BG palette banks + 8 sprites across sprite banks.
 *
 * Gfx: scripts/gen_mcr_demo_gfx.py → demo_gfx.h
 */
#include "mcr.h"
#include "demo_gfx.h"
#include <string.h>

void main(void);
void mcr_boot(void);

volatile byte mcr_vblank_flag;

void mcr_vblank_isr(void) __naked {
__asm
  push af
  push bc
  push de
  push hl
  push ix
  push iy
  xor  a
  inc  a
  ld   (_mcr_vblank_flag), a
  pop  iy
  pop  ix
  pop  hl
  pop  de
  pop  bc
  pop  af
  ei
  reti
__endasm;
}

void start(void) __naked {
__asm
  .area _HEADER (ABS)
  .org 0x0000
  ld   sp, #0xE800
  di
  jp   _mcr_boot
  .org 0x7F00
  .dw  _mcr_vblank_isr
  .dw  _mcr_vblank_isr
  .dw  _mcr_vblank_isr
  .dw  _mcr_vblank_isr
  .area _CODE
__endasm;
}

void mcr_boot(void) {
  mcr_enable_vblank_irq();
  main();
}

/* 91464: attrib pal 0→bank48, 1→32, 2→16, 3→0. Sprite pens use +5..+7. */
static void setup_palette(void) {
  static const byte bg[4][3][3] = {
    { {0,0,7}, {7,7,7}, {7,7,0} }, /* bank 0: blue / white / yellow */
    { {7,0,0}, {7,4,0}, {7,7,7} }, /* bank 1: red / orange / white */
    { {0,7,0}, {0,7,7}, {7,7,7} }, /* bank 2: green / cyan / white */
    { {7,0,7}, {7,7,0}, {7,7,7} }, /* bank 3: magenta / yellow / white */
  };
  static const byte spr[4][3][3] = {
    { {7,7,0}, {7,0,0}, {0,7,0} },
    { {0,7,7}, {0,0,7}, {7,7,7} },
    { {7,3,0}, {7,0,7}, {3,7,3} },
    { {3,3,7}, {7,5,0}, {5,5,5} },
  };
  byte b, p;
  for (b = 0; b < 4; b++) {
    byte base = (byte)(b * 16);
    set_color(base, 0, 0, 0);
    for (p = 0; p < 3; p++)
      set_color((byte)(base + 1 + p), bg[b][p][0], bg[b][p][1], bg[b][p][2]);
    /* sprite pens 5–7 share the bank */
    for (p = 0; p < 3; p++)
      set_color((byte)(base + 5 + p), spr[b][p][0], spr[b][p][1], spr[b][p][2]);
  }
}

void main(void) {
  byte x, y, i;
  byte t = 0;

  mcr_init();
  (void)demo_bg_gfx[0];
  (void)demo_spr_gfx[0];

  setup_palette();

  memset(vram, 0, 0x800);
  for (y = 0; y < TILE_ROWS; y++) {
    for (x = 0; x < TILE_COLS; x++) {
      byte code = (byte)(T_SOLID + ((x + y) & 3));
      if (code > T_HATCH) code = T_SOLID;
      set_tile(x, y, code, (byte)((x >> 3) & 3), 0, 0);
    }
  }

  while (1) {
    wait_vblank();
    t++;
    for (i = 0; i < 8; i++) {
      word sx = (word)(40 + i * 48);
      word sy = (word)(60 + ((t + i * 16) & 63));
      /* cycle sprite codes + all 4 91464 palette banks */
      set_sprite_xy(i, (byte)(i & 1), (byte)(i & 3), sx, sy);
    }
  }
}
